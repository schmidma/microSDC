#include "DPWSHost.hpp"
#include "MicroSDC.hpp"
#include "datamodel/ExpectedElement.hpp"
#include "datamodel/MessageModel.hpp"
#include "datamodel/MessageSerializer.hpp"
#include "esp_pthread.h"
#include <array>
#include <esp_log.h>
#include <memory>

static constexpr const char* TAG = "DPWS";

DPWSHost::DPWSHost(WS::ADDRESSING::EndpointReferenceType::AddressType epr,
                   WS::DISCOVERY::QNameListType types, WS::DISCOVERY::UriListType xAddresses,
                   WS::DISCOVERY::HelloType::MetadataVersionType metadataVersion)
  : socket_(ioContext_,
            asio::ip::udp::endpoint(asio::ip::udp::v4(), MDPWS::UDP_MULTICAST_DISCOVERY_PORT))
  , multicastEndpoint_(addressFromString(MDPWS::UDP_MULTICAST_DISCOVERY_IP_V4),
                       MDPWS::UDP_MULTICAST_DISCOVERY_PORT)
  , receiveBuffer_(std::make_unique<std::array<char, MDPWS::MAX_ENVELOPE_SIZE + 1>>())
  , endpointReference_(epr)
  , types_(types)
  , xAddresses_(std::move(xAddresses))
  , metadataVersion_(metadataVersion)
{
  socket_.set_option(asio::ip::udp::socket::reuse_address(true));
  socket_.set_option(asio::ip::multicast::join_group(multicastEndpoint_.address()));
}

void DPWSHost::stop()
{
  ESP_LOGI(TAG, "Stopping...");
  running_.store(false);
  socket_.close();
  ioContext_.stop();
  thread_.join();
}

void DPWSHost::start()
{
  esp_pthread_cfg_t pthreadConf = esp_pthread_get_default_config();
  pthreadConf.stack_size = 4096;
  esp_pthread_set_cfg(&pthreadConf);
  thread_ = std::thread([&]() {
    running_.store(true);
    sendHello();
    ESP_LOGI(TAG, "Start listening for discovery messages...");
    doReceive();
    ioContext_.run();
    ESP_LOGI(TAG, "Shutting down dpws thread...");
  });
}

bool DPWSHost::running() const
{
  return running_.load();
}

void DPWSHost::doReceive()
{
  if (running_.load())
  {
    socket_.async_receive_from(
        asio::buffer(receiveBuffer_->data(), receiveBuffer_->size()), senderEndpoint_,
        [this](const std::error_code& error, std::size_t bytesRecvd) {
          ESP_LOGD(TAG, "Received %d bytes, ec: %s", bytesRecvd, error.message().c_str());
          // null terminate whatever received
          receiveBuffer_->at(bytesRecvd) = '\0';
          if (!error)
          {
            handleUDPMessage(bytesRecvd);
          }
          doReceive();
        });
  }
}

asio::ip::address_v4 DPWSHost::addressFromString(const char* addressString)
{
  asio::ip::address_v4::bytes_type addressBytes;
  if (inet_pton(AF_INET, addressString, &addressBytes) <= 0)
  {
    throw std::runtime_error("Cannot create ip address from string!");
  }
  return asio::ip::address_v4(addressBytes);
}

void DPWSHost::handleUDPMessage(std::size_t bytesRecvd)
{
  std::array<char, 32> addressString;
  const auto addressBytes = senderEndpoint_.address().to_v4().to_bytes();
  inet_ntop(AF_INET, &addressBytes, addressString.data(), addressString.size());
  ESP_LOGD(TAG, "Received %d bytes from %s:", bytesRecvd, addressString.data());
  ESP_LOGD(TAG, "%s", receiveBuffer_->data());

  rapidxml::xml_document<> doc;
  try
  {
    doc.parse<rapidxml::parse_fastest>(receiveBuffer_->data());
  }
  catch (const rapidxml::parse_error& e)
  {
    ESP_LOGE(TAG, "ParseError at %c (%d): %s", *e.where<char>(),
             e.where<char>() - receiveBuffer_->data(), e.what());
  }
  auto envelopeNode = doc.first_node("Envelope", MDPWS::WS_NS_SOAP_ENVELOPE);
  if (envelopeNode == nullptr)
  {
    ESP_LOGE(TAG, "Cannot find soap envelope node in received message!");
    return;
  }
  std::unique_ptr<MESSAGEMODEL::Envelope> envelope;

  try
  {
    envelope = std::make_unique<MESSAGEMODEL::Envelope>(*envelopeNode);
  }
  catch (ExpectedElement& e)
  {
    ESP_LOGE(TAG, "ExpectedElement %s:%s not encountered\n", e.namespace_().c_str(),
             e.name().c_str());
    return;
  }

  if (envelope->Body().Probe().has_value())
  {
    ESP_LOGD(TAG, "Received Probe from %s", addressString.data());
    handleProbe(*envelope);
  }
  else if (envelope->Body().Bye().has_value())
  {
    ESP_LOGD(TAG, "Received WS-Discovery Bye message from %s", addressString.data());
  }
  else if (envelope->Body().Hello().has_value())
  {
    ESP_LOGD(TAG, "Received WS-Discovery Hello message from %s", addressString.data());
  }
  else if (envelope->Body().ProbeMatches().has_value())
  {
    ESP_LOGD(TAG, "Received WS-Discovery ProbeMatches message from %s", addressString.data());
  }
  else if (envelope->Body().Resolve().has_value())
  {
    ESP_LOGD(TAG, "Received WS-Discovery Resolve message from %s asking for EndpointReference %s",
             addressString.data(),
             envelope->Body().Resolve()->EndpointReference().Address().uri().c_str());
    handleResolve(*envelope);
  }
  else if (envelope->Body().ResolveMatches().has_value())
  {
    ESP_LOGD(TAG, "Received WS-Discovery ResolveMatches message from %s", addressString.data());
  }
  else
  {
    ESP_LOGW(TAG, "Received unhandled UDP message");
  }
  doc.clear();
}

void DPWSHost::sendHello()
{
  messagingContext_.resetInstanceId();
  // Construct Hello Message
  auto message = std::make_unique<MESSAGEMODEL::Envelope>();
  buildHelloMessage(*message);
  MESSAGEMODEL::Envelope::HeaderType::AppSequenceType appSequence(
      messagingContext_.getInstanceId(), messagingContext_.getNextMessageCounter());
  message->Header().AppSequence() = appSequence;
  // Serialize and send
  MessageSerializer serializer;
  serializer.serialize(*message);
  auto msg = std::make_shared<std::string>(serializer.str());
  ESP_LOGI(TAG, "Sending hello message...");
  socket_.async_send_to(
      asio::buffer(*msg), multicastEndpoint_,
      [msg](const std::error_code& ec, const std::size_t bytesTransferred) {
        if (ec)
        {
          ESP_LOGE(TAG, "Error while sending Hello: ec %d, %s", ec.value(), ec.message().c_str());
          return;
        }
        ESP_LOGD(TAG, "Sent hello msg (%d bytes): \n %s", bytesTransferred, msg->c_str());
      });
}

void DPWSHost::buildHelloMessage(MESSAGEMODEL::Envelope& envelope)
{
  envelope.Header().Action() = WS::ADDRESSING::URIType(MDPWS::WS_ACTION_HELLO);
  envelope.Header().To() = WS::ADDRESSING::URIType(MDPWS::WS_DISCOVERY_URN);
  envelope.Header().MessageID() = MicroSDC::calculateMessageID();
  auto& hello = envelope.Body().Hello() =
      WS::DISCOVERY::HelloType(endpointReference_, metadataVersion_);
  if (!scopes_.empty())
  {
    hello->Scopes(scopes_);
  }
  if (!types_.empty())
  {
    hello->Types(types_);
  }
  if (!xAddresses_.empty())
  {
    hello->XAddrs(xAddresses_);
  }
}

void DPWSHost::handleProbe(const MESSAGEMODEL::Envelope& envelope)
{
  auto responseMessage = std::make_unique<MESSAGEMODEL::Envelope>();
  buildProbeMatchMessage(*responseMessage, envelope);
  MessageSerializer serializer;
  serializer.serialize(*responseMessage);
  ESP_LOGI(TAG, "Sending ProbeMatch");
  auto msg = std::make_shared<std::string>(serializer.str());
  socket_.async_send_to(asio::buffer(*msg), senderEndpoint_,
                        [msg](const std::error_code& ec, const std::size_t bytesTransferred) {
                          if (ec)
                          {
                            ESP_LOGE(TAG, "Error while sending ProbeMatch: ec %d, %s", ec.value(),
                                     ec.message().c_str());
                            return;
                          }
                          ESP_LOGD(TAG, "Sent ProbeMatch msg (%d bytes): \n %s", bytesTransferred,
                                   msg->c_str());
                        });
}

void DPWSHost::handleResolve(const MESSAGEMODEL::Envelope& envelope)
{
  if (envelope.Body().Resolve()->EndpointReference().Address().uri() != endpointReference_.uri())
  {
    return;
  }
  auto responseMessage = std::make_unique<MESSAGEMODEL::Envelope>();
  buildResolveMatchMessage(*responseMessage, envelope);
  MessageSerializer serializer;
  serializer.serialize(*responseMessage);
  ESP_LOGI(TAG, "Sending ResolveMatch");
  auto msg = std::make_shared<std::string>(serializer.str());
  socket_.async_send_to(asio::buffer(*msg), senderEndpoint_,
                        [msg](const std::error_code& ec, const std::size_t bytesTransferred) {
                          if (ec)
                          {
                            ESP_LOGE(TAG, "Error while sending ResolveMatch: ec %d, %s", ec.value(),
                                     ec.message().c_str());
                            return;
                          }
                          ESP_LOGD(TAG, "Sent ResolveMatch msg (%d bytes): \n %s", bytesTransferred,
                                   msg->c_str());
                        });
}

void DPWSHost::buildProbeMatchMessage(MESSAGEMODEL::Envelope& envelope,
                                      const MESSAGEMODEL::Envelope& request)
{
  auto& probeMatches = envelope.Body().ProbeMatches() = WS::DISCOVERY::ProbeMatchesType();
  // TODO: check for match
  auto& match = probeMatches->ProbeMatch().emplace_back(endpointReference_, metadataVersion_);
  if (!scopes_.empty())
  {
    match.Scopes(scopes_);
  }
  if (!types_.empty())
  {
    match.Types(types_);
  }
  if (!xAddresses_.empty())
  {
    match.XAddrs(xAddresses_);
  }

  envelope.Header().Action() = WS::ADDRESSING::URIType(MDPWS::WS_ACTION_PROBE_MATCHES);
  if (request.Header().ReplyTo().has_value())
  {
    envelope.Header().To() = request.Header().ReplyTo()->Address();
  }
  else
  {
    envelope.Header().To() = WS::ADDRESSING::URIType(MDPWS::WS_ADDRESSING_ANONYMOUS);
  }
  if (request.Header().MessageID().has_value())
  {
    envelope.Header().RelatesTo() = request.Header().MessageID().value();
  }
  envelope.Header().MessageID() = MicroSDC::calculateMessageID();
}

void DPWSHost::buildResolveMatchMessage(MESSAGEMODEL::Envelope& envelope,
                                        const MESSAGEMODEL::Envelope& request)
{
  auto& resolveMatches = envelope.Body().ResolveMatches() = WS::DISCOVERY::ResolveMatchesType();
  auto& match = resolveMatches->ResolveMatch().emplace_back(endpointReference_, metadataVersion_);
  if (!scopes_.empty())
  {
    match.Scopes(scopes_);
  }
  if (!types_.empty())
  {
    match.Types(types_);
  }
  if (!xAddresses_.empty())
  {
    match.XAddrs(xAddresses_);
  }

  envelope.Header().Action() = WS::ADDRESSING::URIType(MDPWS::WS_ACTION_RESOLVE_MATCHES);
  if (request.Header().ReplyTo().has_value())
  {
    envelope.Header().To() = request.Header().ReplyTo()->Address();
  }
  else
  {
    envelope.Header().To() = WS::ADDRESSING::URIType(MDPWS::WS_ADDRESSING_ANONYMOUS);
  }
  if (request.Header().MessageID().has_value())
  {
    envelope.Header().RelatesTo() = request.Header().MessageID().value();
  }
  envelope.Header().MessageID() = MicroSDC::calculateMessageID();
}
