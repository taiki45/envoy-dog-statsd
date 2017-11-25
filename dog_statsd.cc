#include "dog_statsd.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {
namespace Stats {
namespace DogStatsd {

Writer::Writer(Network::Address::InstanceConstSharedPtr address) {
  fd_ = address->socket(Network::Address::SocketType::Datagram);
  ASSERT(fd_ != -1);

  int rc = address->connect(fd_);
  ASSERT(rc != -1);
  UNREFERENCED_PARAMETER(rc);
}

Writer::~Writer() {
  if (fd_ != -1) {
    RELEASE_ASSERT(close(fd_) == 0);
  }
}

void Writer::writeCounter(const std::string& name, uint64_t increment) {
  std::string message(fmt::format("envoy.{}:{}|c", name, increment));
  send(message);
}

void Writer::writeGauge(const std::string& name, uint64_t value) {
  std::string message(fmt::format("envoy.{}:{}|g", name, value));
  send(message);
}

void Writer::writeTimer(const std::string& name, const std::chrono::milliseconds& ms) {
  std::string message(fmt::format("envoy.{}:{}|ms", name, ms.count()));
  send(message);
}

void Writer::send(const std::string& message) {
  ::send(fd_, message.c_str(), message.size(), MSG_DONTWAIT);
}

UdpStatsdSink::UdpStatsdSink(ThreadLocal::SlotAllocator& tls,
                             Network::Address::InstanceConstSharedPtr address)
    : tls_(tls.allocateSlot()), server_address_(address) {
  tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<Writer>(this->server_address_);
  });
}

void UdpStatsdSink::flushCounter(const Counter& counter, uint64_t delta) {
  tls_->getTyped<Writer>().writeCounter(counter.name(), delta);
}

void UdpStatsdSink::flushGauge(const Gauge& gauge, uint64_t value) {
  tls_->getTyped<Writer>().writeGauge(gauge.name(), value);
}

void UdpStatsdSink::onHistogramComplete(const Histogram& histogram, uint64_t value) {
  // For statsd histograms are all timers.
  tls_->getTyped<Writer>().writeTimer(histogram.name(), std::chrono::milliseconds(value));
}

char TcpStatsdSink::STAT_PREFIX[] = "envoy.";

TcpStatsdSink::TcpStatsdSink(const LocalInfo::LocalInfo& local_info,
                             const std::string& cluster_name, ThreadLocal::SlotAllocator& tls,
                             Upstream::ClusterManager& cluster_manager, Stats::Scope& scope)
    : tls_(tls.allocateSlot()), cluster_manager_(cluster_manager),
      cx_overflow_stat_(scope.counter("statsd.cx_overflow")) {

  Config::Utility::checkClusterAndLocalInfo("tcp statsd", cluster_name, cluster_manager,
                                            local_info);
  cluster_info_ = cluster_manager.get(cluster_name)->info();
  tls_->set([this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<TlsSink>(*this, dispatcher);
  });
}

TcpStatsdSink::TlsSink::TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher)
    : parent_(parent), dispatcher_(dispatcher) {}

TcpStatsdSink::TlsSink::~TlsSink() {
  if (connection_) {
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpStatsdSink::TlsSink::beginFlush(bool expect_empty_buffer) {
  ASSERT(!expect_empty_buffer || buffer_.length() == 0);
  ASSERT(current_slice_mem_ == nullptr);
  UNREFERENCED_PARAMETER(expect_empty_buffer);

  uint64_t num_iovecs = buffer_.reserve(FLUSH_SLICE_SIZE_BYTES, &current_buffer_slice_, 1);
  ASSERT(num_iovecs == 1);
  UNREFERENCED_PARAMETER(num_iovecs);

  ASSERT(current_buffer_slice_.len_ >= FLUSH_SLICE_SIZE_BYTES);
  current_slice_mem_ = reinterpret_cast<char*>(current_buffer_slice_.mem_);
}

void TcpStatsdSink::TlsSink::commonFlush(const std::string& name, uint64_t value, char stat_type) {
  ASSERT(current_slice_mem_ != nullptr);
  // 40 > 6 (prefix) + 4 (random chars) + 30 for number (bigger than it will ever be)
  const uint32_t max_size = name.size() + 40;
  if (current_buffer_slice_.len_ - usedBuffer() < max_size) {
    endFlush(false);
    beginFlush(false);
  }

  // Produces something like "envoy.{}:{}|c\n"
  // This written this way for maximum perf since with a large number of stats and at a high flush
  // rate this can become expensive.
  const char* snapped_current = current_slice_mem_;
  memcpy(current_slice_mem_, STAT_PREFIX, sizeof(STAT_PREFIX) - 1);
  current_slice_mem_ += sizeof(STAT_PREFIX) - 1;
  memcpy(current_slice_mem_, name.c_str(), name.size());
  current_slice_mem_ += name.size();
  *current_slice_mem_++ = ':';
  current_slice_mem_ += StringUtil::itoa(current_slice_mem_, 30, value);
  *current_slice_mem_++ = '|';
  *current_slice_mem_++ = stat_type;
  *current_slice_mem_++ = '\n';

  ASSERT(static_cast<uint64_t>(current_slice_mem_ - snapped_current) < max_size);
  UNREFERENCED_PARAMETER(snapped_current);
}

void TcpStatsdSink::TlsSink::flushCounter(const std::string& name, uint64_t delta) {
  commonFlush(name, delta, 'c');
}

void TcpStatsdSink::TlsSink::flushGauge(const std::string& name, uint64_t value) {
  commonFlush(name, value, 'g');
}

void TcpStatsdSink::TlsSink::endFlush(bool do_write) {
  ASSERT(current_slice_mem_ != nullptr);
  current_buffer_slice_.len_ = usedBuffer();
  buffer_.commit(&current_buffer_slice_, 1);
  current_slice_mem_ = nullptr;
  if (do_write) {
    write(buffer_);
  }
}

void TcpStatsdSink::TlsSink::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    dispatcher_.deferredDelete(std::move(connection_));
  }
}

void TcpStatsdSink::TlsSink::onTimespanComplete(const std::string& name,
                                                std::chrono::milliseconds ms) {
  // Ultimately it would be nice to perf optimize this path also, but it's not very frequent. It's
  // also currently not possible that this interleaves with any counter/gauge flushing.
  ASSERT(current_slice_mem_ == nullptr);
  Buffer::OwnedImpl buffer(fmt::format("envoy.{}:{}|ms\n", name, ms.count()));
  write(buffer);
}

void TcpStatsdSink::TlsSink::write(Buffer::Instance& buffer) {
  // Guard against the stats connection backing up. In this case we probably have no visibility
  // into what is going on externally, but we also increment a stat that should be viewable
  // locally.
  // NOTE: In the current implementation, we write most stats on the main thread, but timers
  //       get emitted on the worker threads. Since this is using global buffered data, it's
  //       possible that we are about to kill the connection that is not actually backed up.
  //       This is essentially a panic state, so it's not worth keeping per thread buffer stats,
  //       since if we stay over, the other threads will eventually kill their connections too.
  // TODO(mattklein123): The use of the stat is somewhat of a hack, and should be replaced with
  // real flow control callbacks once they are available.
  if (parent_.cluster_info_->stats().upstream_cx_tx_bytes_buffered_.value() >
      MAX_BUFFERED_STATS_BYTES) {
    if (connection_) {
      connection_->close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.cx_overflow_stat_.inc();
    buffer.drain(buffer.length());
    return;
  }

  if (!connection_) {
    Upstream::Host::CreateConnectionData info =
        parent_.cluster_manager_.tcpConnForCluster(parent_.cluster_info_->name(), nullptr);
    if (!info.connection_) {
      return;
    }

    connection_ = std::move(info.connection_);
    connection_->addConnectionCallbacks(*this);
    connection_->setConnectionStats({parent_.cluster_info_->stats().upstream_cx_rx_bytes_total_,
                                     parent_.cluster_info_->stats().upstream_cx_rx_bytes_buffered_,
                                     parent_.cluster_info_->stats().upstream_cx_tx_bytes_total_,
                                     parent_.cluster_info_->stats().upstream_cx_tx_bytes_buffered_,
                                     &parent_.cluster_info_->stats().bind_errors_});
    connection_->connect();
  }

  connection_->write(buffer);
}

uint64_t TcpStatsdSink::TlsSink::usedBuffer() {
  ASSERT(current_slice_mem_ != nullptr);
  return current_slice_mem_ - reinterpret_cast<char*>(current_buffer_slice_.mem_);
}

} // namespace Statsd
} // namespace Stats
} // namespace Envoy
