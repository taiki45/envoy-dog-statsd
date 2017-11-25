#include "dog_statsd_config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/resolver_impl.h"
#include "dog_statsd.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr DogStatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                  Server::Instance& server) {

  const auto& statsd_sink = dynamic_cast<const envoy::api::v2::StatsdSink&>(config);
  switch (statsd_sink.statsd_specifier_case()) {
  case envoy::api::v2::StatsdSink::kTcpClusterName:
    ENVOY_LOG(debug, "DogStatsd TCP cluster: {}", statsd_sink.tcp_cluster_name());
    return Stats::SinkPtr(new Stats::DogStatsd::TcpStatsdSink(
        server.localInfo(), statsd_sink.tcp_cluster_name(), server.threadLocal(),
        server.clusterManager(), server.stats()));
    break;
  default:
    throw EnvoyException(
        fmt::format("No tcp_cluster_name or address provided for {} Stats::Sink config", name()));
  }
}

ProtobufTypes::MessagePtr DogStatsdSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::api::v2::StatsdSink>(new envoy::api::v2::StatsdSink());
}

std::string DogStatsdSinkFactory::name() { return "envoy.dog_statsd"; }

/**
 * Static registration for the dog_statsd sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<DogStatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
