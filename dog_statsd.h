#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Stats {
namespace DogStatsd {

/**
 * This is a simple UDP localhost writer for statsd messages.
 */
class Writer : public ThreadLocal::ThreadLocalObject {
public:
  Writer(Network::Address::InstanceConstSharedPtr address);
  ~Writer();

  void writeCounter(const std::string& name, uint64_t increment);
  void writeGauge(const std::string& name, uint64_t value);
  void writeTimer(const std::string& name, const std::chrono::milliseconds& ms);

  // Called in unit test to validate address.
  int getFdForTests() const { return fd_; };

private:
  void send(const std::string& message);

  int fd_;
};

/**
 * Implementation of Sink that writes to a UDP statsd address.
 */
class UdpStatsdSink : public Sink {
public:
  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, Network::Address::InstanceConstSharedPtr address);

  // Stats::Sink
  void beginFlush() override {}
  void flushCounter(const Counter& counter, uint64_t delta) override;
  void flushGauge(const Gauge& gauge, uint64_t value) override;
  void endFlush() override {}
  void onHistogramComplete(const Histogram& histogram, uint64_t value) override;

  // Called in unit test to validate writer construction and address.
  int getFdForTests() { return tls_->getTyped<Writer>().getFdForTests(); }

private:
  ThreadLocal::SlotPtr tls_;
  Network::Address::InstanceConstSharedPtr server_address_;
};

/**
 * Per thread implementation of a TCP stats flusher for statsd.
 */
class TcpStatsdSink : public Sink {
public:
  TcpStatsdSink(const LocalInfo::LocalInfo& local_info, const std::string& cluster_name,
                ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cluster_manager,
                Stats::Scope& scope);

  // Stats::Sink
  void beginFlush() override { tls_->getTyped<TlsSink>().beginFlush(true); }

  void flushCounter(const Counter& counter, uint64_t delta) override {
    tls_->getTyped<TlsSink>().flushCounter(counter.name(), delta);
  }

  void flushGauge(const Gauge& gauge, uint64_t value) override {
    tls_->getTyped<TlsSink>().flushGauge(gauge.name(), value);
  }

  void endFlush() override { tls_->getTyped<TlsSink>().endFlush(true); }

  void onHistogramComplete(const Histogram& histogram, uint64_t value) override {
    // For statsd histograms are all timers.
    tls_->getTyped<TlsSink>().onTimespanComplete(histogram.name(),
                                                 std::chrono::milliseconds(value));
  }

private:
  struct TlsSink : public ThreadLocal::ThreadLocalObject, public Network::ConnectionCallbacks {
    TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher);
    ~TlsSink();

    void beginFlush(bool expect_empty_buffer);
    void checkSize();
    void commonFlush(const std::string& name, uint64_t value, char stat_type);
    void flushCounter(const std::string& name, uint64_t delta);
    void flushGauge(const std::string& name, uint64_t value);
    void endFlush(bool do_write);
    void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms);
    uint64_t usedBuffer();
    void write(Buffer::Instance& buffer);

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    TcpStatsdSink& parent_;
    Event::Dispatcher& dispatcher_;
    Network::ClientConnectionPtr connection_;
    Buffer::OwnedImpl buffer_;
    Buffer::RawSlice current_buffer_slice_;
    char* current_slice_mem_{};
  };

  // Somewhat arbitrary 16MiB limit for buffered stats.
  static constexpr uint32_t MAX_BUFFERED_STATS_BYTES = (1024 * 1024 * 16);

  // 16KiB intermediate buffer for flushing.
  static constexpr uint32_t FLUSH_SLICE_SIZE_BYTES = (1024 * 16);

  // Prefix for all flushed stats.
  static char STAT_PREFIX[];

  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  ThreadLocal::SlotPtr tls_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::Counter& cx_overflow_stat_;
};

} // namespace Statsd
} // namespace Stats
} // namespace Envoy
