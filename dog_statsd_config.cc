#include "dog_statsd_config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/resolver_impl.h"
#include "dog_statsd.h"

#include "dog_statsd_config.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr DogStatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                  Server::Instance& server) {

  const auto& statsd_sink = dynamic_cast<const envoy::dog_statsd::config::StatsdSink&>(config);
    ENVOY_LOG(debug, "DogStatsd TCP cluster: {}", statsd_sink.tcp_cluster_name());
    return Stats::SinkPtr(new Stats::DogStatsd::TcpStatsdSink(
        server.localInfo(), statsd_sink.tcp_cluster_name(), server.threadLocal(),
        server.clusterManager(), server.stats()));
  }

ProtobufTypes::MessagePtr DogStatsdSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::dog_statsd::config::StatsdSink>(new envoy::dog_statsd::config::StatsdSink());
}

std::string DogStatsdSinkFactory::name() { return "envoy.dog_statsd"; }

/**
 * Static registration for the dog_statsd sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<DogStatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
