#pragma once

#include "DeviceCharacteristics.hpp"
#include "datamodel/MessageModel.hpp"
#include "datamodel/ws-MetadataExchange.hpp"

class MetadataProvider
{
public:
  using Host = WS::DPWS::HostServiceType;
  using Hosted = WS::DPWS::HostedServiceType;
  using HostedSequence = WS::DPWS::Relationship::HostedSequence;
  using MetadataSection = WS::MEX::MetadataSection;

  /**
   * @brief constructs a MetadataProvider object from given Device Characteristics
   * @param devChar Device Characteristics to provide with this MetadataProvider
   * @param useTLS whether TLS should is used for SDC communication
   */
  MetadataProvider(const DeviceCharacteristics& devChar, bool useTLS);

  /**
   * @brief get the URI of the Device Service
   * @return string containing the URI
   */
  std::string getDeviceServicePath() const;
  /**
   * @brief get the URI of the GetService
   * @return string containing the URI
   */
  std::string getGetServicePath() const;
  /**
   * @brief get the URI of the SetService
   * @return string containing the URI
   */
  std::string getSetServicePath() const;
  /**
   * @brief gets the endpoint URI of the SetService
   * @return URI containing information regarding the set service
   */
  WS::ADDRESSING::URIType getSetServiceURI() const;
  /**
   * @brief fill SOAP Envelope with Device Metadata for GetMetadataResponse
   * @param envelope reference to the response envelope to fill
   */
  void fillDeviceMetadata(MESSAGEMODEL::Envelope& envelope) const;
  /**
   * @brief fill SOAP Envelope with GetService Metadata for GetMetadataResponse
   * @param envelope reference to the response envelope to fill
   */
  void fillGetServiceMetadata(MESSAGEMODEL::Envelope& envelope) const;
  /**
   * @brief fill SOAP Envelope with SetService Metadata for GetMetadataResponse
   * @param envelope reference to the response envelope to fill
   */
  void fillSetServiceMetadata(MESSAGEMODEL::Envelope& envelope) const;
  /**
   * @brief compile host part of MetadataSection Relationship
   * @return Host section
   */
  Host createHostMetadata() const;
  /**
   * @brief compile MetadataSection ThisModel containing information like e.g. manufacturer
   * @return filled ThisModel section
   */
  MetadataSection createMetadataSectionThisModel() const;
  /**
   * @brief compile MetadataSection ThisDevice containing information like e.g. friendly name
   * @return filled ThisDevice section
   */
  MetadataSection createMetadataSectionThisDevice() const;
  /**
   * @brief compile MetadataSection Relationship connecting Host with Hosted Services
   * @param host Host section custructed by createHostMetadata()
   * @param hosted sequence of Hosted services
   * @return filled Relationship section
   */
  MetadataSection createMetadataSectionRelationship(const Host& host,
                                                    const HostedSequence& hosted) const;
  /**
   * @brief compile information about WSDL for GetService
   * @return constructed MetadataSection
   */
  MetadataSection createMetadataSectionWSDLGetService() const;
  /**
   * @brief compile information about WSDL for SetService
   * @return constructed MetadataSection
   */
  MetadataSection createMetadataSectionWSDLSetService() const;
  /**
   * @brief construct Hosted section for GetService
   * @brief Hosted element
   */
  Hosted createHostedGetService() const;
  /**
   * @brief construct Hosted section for SetService
   * @brief Hosted element
   */
  Hosted createHostedSetService() const;

private:
  /// device characteristics to provide
  const DeviceCharacteristics deviceCharacteristics_;
  /// whether the URIs should include https as header
  const bool useTLS;
};
