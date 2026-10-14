#include <iostream>

#include "remus/link/BladeLinkStateMachine.hpp"
#include "remus/link/BindAcceptance.hpp"
#include "remus/link/ComputerPeerStateMachine.hpp"
#include "remus/link/RadioSupervisor.hpp"
#include "remus/link/SourceRegistry.hpp"
#include "remus/link/TxScheduler.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* expression, int line) {
  if (!value) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}
#define EXPECT(value) expect((value), #value, __LINE__)

remus::MacAddress mac(std::uint8_t suffix) { return {{0x02, 0, 0, 0, 0, suffix}}; }

class FakeRadio final : public remus::hal::IRadio {
 public:
  void setEventSink(remus::hal::IRadioEvents* sink) noexcept override { sink_ = sink; }
  remus::Status start(std::uint8_t) noexcept override { return remus::Status::success(); }
  remus::Status setPrimaryMasterKey(const std::uint8_t[16]) noexcept override {
    return remus::Status::success();
  }
  remus::Status addPeer(const remus::MacAddress&, const std::uint8_t[16]) noexcept override {
    return remus::Status::success();
  }
  remus::Status removePeer(const remus::MacAddress&) noexcept override {
    return remus::Status::success();
  }
  remus::Status requestSend(const remus::MacAddress& destination, const std::uint8_t*,
                            std::size_t) noexcept override {
    ++requests;
    lastDestination = destination;
    return remus::Status::success();
  }
  remus::Status stop() noexcept override { return remus::Status::success(); }
  remus::hal::IRadioEvents* sink_{nullptr};
  std::uint32_t requests{0};
  remus::MacAddress lastDestination{};
};

void testBladeRequiresAuthenticatedSequence() {
  remus::link::BladeLinkStateMachine state;
  EXPECT(state.state() == remus::link::BladeLinkState::Boot);
  state.onRadioStarted();
  EXPECT(state.state() == remus::link::BladeLinkState::Discovering);
  EXPECT(state.onBindAccepted(7, 9, 11).error == remus::Error::NotReady);
  EXPECT(state.onAuthenticatedBeacon(100, 7).ok());
  EXPECT(state.state() == remus::link::BladeLinkState::HelloReady);
  EXPECT(state.onBindAccepted(7, 9, 11).ok());
  EXPECT(state.state() == remus::link::BladeLinkState::Bound);
  EXPECT(state.computerBootId() == 7);
  EXPECT(state.recordingId() == 9);
  EXPECT(state.onBindAccepted(7, 9, 11).ok());
  EXPECT(state.onBindAccepted(7, 9, 12).error == remus::Error::InvalidArgument);
}

void testComputerOnlyBindsAfterApplicationAck() {
  remus::link::ComputerPeerStateMachine state;
  EXPECT(state.onAuthenticatedHello(10, 20, 1000).ok());
  EXPECT(state.state() == remus::link::ComputerPeerState::Candidate);
  EXPECT(state.onPeerRegistered().ok());
  EXPECT(state.onBindSent(33).ok());
  EXPECT(state.state() == remus::link::ComputerPeerState::BindPending);
  EXPECT(state.onBindAck(32).error == remus::Error::InvalidArgument);
  EXPECT(state.onBindAck(33).ok());
  EXPECT(state.state() == remus::link::ComputerPeerState::Bound);
}

void testValidBindIsAcknowledgedWhenCaptureCannotStart() {
  const auto decision = remus::link::decideBindAcceptance(
      remus::Status::success(), true,
      remus::Status::failure(remus::Error::IoFailure, 116));
  EXPECT(decision.sendBindAck);
  EXPECT(decision.reportCaptureFailure);
  EXPECT(decision.bindResultCode == 0);
}

void testRegistryBoundsAndIdentityCollision() {
  remus::link::SourceRegistry registry;
  for (std::uint8_t index = 1; index <= 8; ++index) {
    EXPECT(registry.admit(1000 + index, 2000 + index, mac(index), 500).ok());
  }
  EXPECT(registry.size() == 8);
  EXPECT(registry.admit(9999, 9999, mac(9), 500).error == remus::Error::PeerFull);
  EXPECT(registry.admit(1001, 2001, mac(10), 500).error == remus::Error::AuthFailed);
}

void testCandidateExpiryIsAbsolute() {
  remus::link::SourceRegistry registry;
  EXPECT(registry.admit(10, 20, mac(1), 100).ok());
  EXPECT(registry.admit(10, 20, mac(1), 900).ok());
  EXPECT(registry.expireCandidates(1101, 1000) == 1);
  EXPECT(registry.size() == 0);
}

void testRegistryReplacesSameBladeAfterReboot() {
  remus::link::SourceRegistry registry;
  EXPECT(registry.admit(10, 20, mac(1), 100).ok());
  auto* original = registry.find(10, 20);
  EXPECT(original != nullptr);
  EXPECT(original->state.onPeerRegistered().ok());
  EXPECT(original->state.onBindSent(33).ok());
  EXPECT(original->state.onBindAck(33).ok());
  EXPECT(registry.boundCount() == 1);

  EXPECT(registry.admit(10, 21, mac(1), 500).ok());
  EXPECT(registry.size() == 1);
  EXPECT(registry.find(10, 20) == nullptr);
  auto* restarted = registry.find(10, 21);
  EXPECT(restarted != nullptr);
  EXPECT(restarted->state.state() == remus::link::ComputerPeerState::Candidate);
  EXPECT(registry.boundCount() == 0);

  EXPECT(registry.admit(10, 22, mac(2), 600).error == remus::Error::AuthFailed);
  EXPECT(registry.admit(11, 22, mac(1), 600).error == remus::Error::AuthFailed);
}

void testRegistryReportsOnlyRecentlySeenBoundBladesAsLive() {
  remus::link::SourceRegistry registry;
  EXPECT(registry.admit(10, 20, mac(1), 100).ok());
  auto* source = registry.find(10, 20);
  EXPECT(source != nullptr);
  EXPECT(source->state.onPeerRegistered().ok());
  EXPECT(source->state.onBindSent(33).ok());
  EXPECT(source->state.onBindAck(33).ok());
  EXPECT(registry.liveBoundCount(1099, 1000) == 1);
  EXPECT(registry.liveBoundCount(1101, 1000) == 0);
}

void testRegistryCountsOnlyFullyBoundBlades() {
  remus::link::SourceRegistry registry;
  EXPECT(registry.admit(10, 20, mac(1), 100).ok());
  EXPECT(registry.admit(11, 21, mac(2), 100).ok());
  EXPECT(registry.boundCount() == 0);

  auto* first = registry.find(10, 20);
  EXPECT(first != nullptr);
  EXPECT(first->state.onPeerRegistered().ok());
  EXPECT(first->state.onBindSent(33).ok());
  EXPECT(registry.boundCount() == 0);
  EXPECT(first->state.onBindAck(33).ok());
  EXPECT(registry.boundCount() == 1);
}

void testTxSchedulerKeepsOneFrameInFlight() {
  FakeRadio radio;
  remus::link::TxScheduler scheduler(radio);
  const std::uint8_t bytes[]{1, 2, 3};
  EXPECT(scheduler.enqueue(mac(1), bytes, sizeof(bytes)).ok());
  EXPECT(scheduler.enqueue(mac(2), bytes, sizeof(bytes)).ok());
  EXPECT(scheduler.poll(100).ok());
  EXPECT(radio.requests == 1);
  EXPECT(scheduler.poll(200).ok());
  EXPECT(radio.requests == 1);
  scheduler.onSendComplete(mac(1), true);
  EXPECT(scheduler.poll(300).ok());
  EXPECT(radio.requests == 2);
}

void testTxSchedulerRequiresDriverRecoveryAfterMissingCallback() {
  FakeRadio radio;
  remus::link::TxScheduler scheduler(radio);
  const std::uint8_t bytes[]{1, 2, 3};
  EXPECT(scheduler.enqueue(mac(1), bytes, sizeof(bytes)).ok());
  EXPECT(scheduler.enqueue(mac(2), bytes, sizeof(bytes)).ok());
  EXPECT(scheduler.poll(100).ok());
  EXPECT(scheduler.poll(500099).ok());
  EXPECT(scheduler.poll(500100).error == remus::Error::Timeout);
  EXPECT(scheduler.recoveryRequired());
  scheduler.onSendComplete(mac(1), true);
  EXPECT(scheduler.stats().lateCallbacks == 1);
  scheduler.resetAfterRadioRestart(500100);
  EXPECT(!scheduler.recoveryRequired());
  EXPECT(scheduler.depth() == 1);
  EXPECT(scheduler.stats().callbackTimeouts == 1);
  EXPECT(scheduler.stats().recoveryResets == 1);
  EXPECT(scheduler.poll(600099).ok());
  EXPECT(radio.requests == 1);
  EXPECT(scheduler.poll(600100).ok());
  EXPECT(radio.requests == 2);
  EXPECT(radio.lastDestination == mac(2));
}

void testRadioSupervisorBacksOffPersistentFailure() {
  remus::link::RadioSupervisor supervisor;
  supervisor.observe(remus::link::RadioHealthEvent::DriverFailure, 100);
  EXPECT(supervisor.evaluate(100) == remus::link::SupervisorAction::RetryFrame);
  supervisor.observe(remus::link::RadioHealthEvent::DriverFailure, 200);
  supervisor.observe(remus::link::RadioHealthEvent::DriverFailure, 300);
  EXPECT(supervisor.evaluate(300) == remus::link::SupervisorAction::EnterDegraded);
  EXPECT(!supervisor.canReinitialize(1000299));
  EXPECT(supervisor.canReinitialize(1000300));
  supervisor.observe(remus::link::RadioHealthEvent::Reinitialized, 1000300);
  EXPECT(supervisor.snapshot().consecutiveFailures == 0);
}
}  // namespace

int main() {
  testBladeRequiresAuthenticatedSequence();
  testComputerOnlyBindsAfterApplicationAck();
  testValidBindIsAcknowledgedWhenCaptureCannotStart();
  testRegistryBoundsAndIdentityCollision();
  testCandidateExpiryIsAbsolute();
  testRegistryReplacesSameBladeAfterReboot();
  testRegistryReportsOnlyRecentlySeenBoundBladesAsLive();
  testRegistryCountsOnlyFullyBoundBlades();
  testTxSchedulerKeepsOneFrameInFlight();
  testTxSchedulerRequiresDriverRecoveryAfterMissingCallback();
  testRadioSupervisorBacksOffPersistentFailure();
  return failures == 0 ? 0 : 1;
}
