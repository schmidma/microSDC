#pragma once

#include "SDCConstants.hpp"
#include "SessionManager/SessionManager.hpp"
#include "datamodel/ws-addressing.hpp"
#include "datamodel/ws-eventing.hpp"
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct esp_http_client;
namespace BICEPS::MM
{
  class EpisodicMetricReport;
} // namespace BICEPS::MM

/// @brief SubscriptionManager manages subscriptions in terms of ws-eventing
class SubscriptionManager
{
public:
  /// @brief dispatches a subscribe request, registers the new subscriber and creates a client
  /// session
  /// @param subscribeRequest the request the client send to subscribe
  /// @return the response generated by processing the subscription request
  WS::EVENTING::SubscribeResponse dispatch(const WS::EVENTING::Subscribe& subscribeRequest);

  /// @brief dispatches a renew request and extends the duration of a subscription
  /// @param renewRequest the request the client send to renew
  /// @param identifier the identifier identifying the subscription to renew
  /// @return the response generated by processing the renew request
  WS::EVENTING::RenewResponse dispatch(const WS::EVENTING::Renew& renewRequest,
                                       const WS::EVENTING::Identifier& identifier);

  /// @brief dispatches a unsubscribe request and removes the given subscriber from the list of
  /// active subscriptions
  /// @param unsubscribeRequest the request the subscriber sent to unsubscribe
  /// @param identifier the identifier identifying the subscription to remove
  void dispatch(const WS::EVENTING::Unsubscribe& unsubscribeRequest,
                const WS::EVENTING::Identifier& identifier);

  /// @brief triggers an event with given report by notifying all subscribers of this event
  /// @param report the report to notify about
  void fireEvent(const BICEPS::MM::EpisodicMetricReport& report);

private:
  /// @brief SubscriptionInformation stores stateful information about a subscription
  struct SubscriptionInformation
  {
    /// @brief constructs a new SubscriptionInformation from given information describing the
    /// subscriber
    /// @param notifyTo the address of the subscriber
    /// @param filter the eventing filters of this subscription
    /// @param expirationTime the time this subscription is valid
    SubscriptionInformation(WS::ADDRESSING::EndpointReferenceType notifyTo,
                            WS::EVENTING::FilterType filter,
                            std::chrono::system_clock::time_point expirationTime)
      : notifyTo(std::move(notifyTo))
      , filter(std::move(filter))
      , expirationTime(expirationTime)
    {
    }

    /// the address of the subscriber
    const WS::ADDRESSING::EndpointReferenceType notifyTo;
    /// the ws eventing filter of this subscripiton
    const WS::EVENTING::FilterType filter;
    /// the time this subscription is valid for
    std::chrono::system_clock::time_point expirationTime;
  };

  /// mutex protecting subscriptions_ map
  mutable std::mutex subscriptionMutex_;
  /// active subscriptions of the subscriber with a unique identifier
  std::map<std::string, SubscriptionInformation> subscriptions_;
  /// a pointer to the SessionManager implementation
  SessionManager sessionManager_;
  /// all allowed subscriptions of this manager
  std::vector<std::string> allowedSubscriptionEventActions_{
      SDC::ACTION_OPERATION_INVOKED_REPORT,
      SDC::ACTION_PERIODIC_ALERT_REPORT,
      SDC::ACTION_EPISODIC_ALERT_REPORT,
      SDC::ACTION_EPISODIC_COMPONENT_REPORT,
      SDC::ACTION_PERIODIC_COMPONENT_REPORT,
      SDC::ACTION_EPISODIC_METRIC_REPORT,
      SDC::ACTION_PERIODIC_METRIC_REPORT,
      SDC::ACTION_EPISODIC_OPERATIONAL_STATE_REPORT,
      SDC::ACTION_PERIODIC_OPERATIONAL_STATE_REPORT};

  /// @brief prints all current subscriptions to DEBUG Log
  void printSubscriptions() const;
};
