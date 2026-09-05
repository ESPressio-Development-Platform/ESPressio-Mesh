# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

This is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`. Validated propagation spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership, admission, topology, routing, forwarding, delivery, clock coordination and target-native memory-accounting foundations.

Do not alter public/distributed semantics, dependency direction, identity/authentication semantics, locked capacities or Policy without Simon Stuart's explicit decision.

### Unresolved public/wire decisions
Do **not** invent concrete ESPressio `PrimitiveFamilyId` allocations, distributed EventTypeId allocation/derivation, conceptual Event MessageId generation scope/strategy, or Group identifier contract.

## Branch scope

Primary: `ESPressio-Mesh` → `structural_realignment_propagation`.

Affected repositories use `structural_realignment_propagation_ESPressio-Mesh`: System, Primitive **singular**, Radio, ESP32, NRF24, State, Command and Event. `ESPressio-MeshAdapters` is architecturally required but has not yet needed a propagation branch/implementation.

## Mandatory Policy/workflow rules

- Ordinary libraries do not call hardware/platform APIs directly; use ESPressio-System abstractions. Hardware concretes belong in platform/technology repositories.
- Reuse existing abstractions; preserve strict SRP and dependency direction. No circular dependency, including optional dependency; stop and ask Simon if one becomes unavoidable.
- Participating manifests/tests/jobs/docs/source references target matching propagation branches.
- Documentation and IDE-compatible doc comments move with APIs. Useful public discrete components receive Arduino IDE + PlatformIO demos where appropriate.
- Internal diagnostics use compile-switchable ESPressio-Logging after inspecting its current API. Optional callback surfaces use ESPressio-Observable; optional Event bridges live with the source subsystem.
- Do not alter version numbering merely because of tranche work. Do not preserve legacy behavior unless Simon explicitly requests it.
- Maintain this file after meaningful implementation/validation/decision/correction changes. At full completion offer it to Simon for download, then remove it from Git.
- If Actions begins failing immediately because of monthly task/job budget exhaustion, record it and stop treating CI as correctness evidence until reset. No such symptom has occurred.

## Frozen architecture

### Layering
System owns foundational platform-neutral identities/services; Primitive owns dependency-neutral conceptual-message vocabulary; Command is asynchronous intent; Event is occurrence; State is authoritative fact/replication; Radio is opaque direct-link physical/link transport with hop-local packetization/reassembly/retry/evidence; Mesh owns membership/topology/routing/control/logical addressing/admission/multi-radio forwarding/end-to-end delivery and must not depend on Command/Event/State; MeshAdapters integrates above those layers; Timing owns synchronization math/discipline/quality while Mesh coordinates root/parent and Radio owns direct timestamp mechanics.

### Identity / primitive
`DeviceIdentifier` exact 16 B permanent transport-independent zero-invalid and never authentication. `MeshIdentifier` exact nonzero 16 B app-supplied. `MembershipIncarnation` exact nonzero 16 B participation instance. `MeshNodeAlias` uint16 Mesh-local only. `RadioIdentifier` uint8, 0 invalid, 1–254 usable, 255 reserved, incarnation-local. `MeshMessageId` uint64 nonzero monotonic/nonwrapping scoped to authenticated DeviceIdentifier+MembershipIncarnation and retained across retries/routes/fragments. ProfileGeneration/TopologyGeneration uint64 nonzero. CanonicalName mandatory Mesh-unique printable ASCII 1–32 B. ContractFingerprint exact 32 B SHA-256 semantic digest, never auth. PrimitiveFamilyId uint16 ranges remain frozen; concrete ESPressio IDs remain unallocated.

### Command / Event / State
Command has no intrinsic wait/requested-operation completion/result timeout/destination/reply/RPC semantics. Existing `CommandResult` is only local parser/validation/middleware/dispatch/callback disposition and its misleading completion/result vocabulary remains cleanup work. Later operation progress/completion belongs to independent Event/State messages, optionally correlated.

Event intrinsic data is EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote is dispatch context; no transport HopCount/response/completion. Distributed EventTypeId and Event MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`; local State is app-owned/non-owningly bound and remote replicas State-owned. Availability is separate from value. Wire concepts Publication/Availability/Subscribe/Unsubscribe/Resynchronize; no ReplicaAcknowledgement/generic Disconnect. StateEpoch is independent of MembershipIncarnation.

### Membership / admission / liveness
Admission: Unknown→Discovered→Authenticating→Validating→Joining→Active. DeviceIdentifier is never authentication. Candidate context contains generation-safe candidate handle, local RadioIdentifier/RadioPeerHandle and explicitly untrusted identity claim. Only separately established AuthenticatedMeshIdentity enters membership. IMeshSecurityAuthority performs security behind Mesh dependency inversion; IMeshAdmissionPolicy runs only after authentication. Reachability Unknown/Reachable/Suspect/Unreachable through injected policy, approximate defaults 5 s/15 s; valid authenticated evidence may restore Reachable immediately. Full unreachable retention 60 s. Active liveness probes 4. Tombstones 64, default ~5 min, deterministic bounded eviction; pressure never weakens auth/admission.

### Routing / delivery / forwarding
Destinations Node/Group/CapabilitySelector/Broadcast. Selective destinations freeze a bounded recipient set then normalize to independent Node deliveries. Broadcast is bounded best-effort flood with one MessageId, no frozen set/complete-delivery promise.

Dedup is per authenticated source+incarnation, high-water plus 128-bit window. Definitive inbound disposition commits; retryable/resource unavailable releases InProgress without commit. Destination ACK is destination-framework delivery confirmation only, distinct from Radio ACK, intermediate next-hop acceptance and requested-operation completion.

Topology is a graph-wide bounded directed graph; each member is authoritative only for its complete outbound set and TopologyGeneration. MaxTopologyLinks=96 graph-wide. No universal RouteCost. Publication materiality/freshness are policy-injected; Expired topology cannot originate a route.

Routing uses injected IRoutingStrategy. ResolvedRoute<16> is disposable local planning state, never source-routing wire state. RouteCache<32> is optimization only. Same-route max attempts3; distinct routes4; immutable deadline outer bound.

RemainingHopLimit decrements only after authenticated successful Mesh next-hop forwarding transition. Planning/cache/Radio admission/Radio completion/Radio peer ACK/failure do not decrement it. ForwardingSubmissionCoordinator resolves current authenticated exact-incarnation direct peer before RadioTransport send. ForwardingAttemptLifecycle remains pending until exact authenticated next-hop acceptance.

### Radio / evidence
Radio accepts complete immutable bounded logical transfers to direct peers and owns fragmentation/reassembly/physical retry. RadioPeerHandle is generation-safe process-local capability only. AuthenticatedDirectPeerBindingTable<96> maps authenticated neighbour incarnation + local RadioIdentifier to executable RadioPeerHandle; peer invalidation removes executable binding only, never membership.

Radio evidence stages are explicitly distinct: synchronous Send attempt/admission; provider-established transmission completion/failure; peer acknowledgement where supported. IRadioSendAttemptObserver is attempt-only. RadioSendResult carries synchronous RadioDirectLinkEvidence. RadioTransmissionCompletion is Unknown/Completed/Failed. Accepted non-terminal work may carry provider-local RadioTransmissionHandle; IRadioTransmissionObserver publishes exactly one terminal observation for a promised handle.

`DeferredLogicalTransferTracker<Capacity>` provides finite composition-owned aggregation of provider-local fragment handles. RadioTransport now optionally receives this tracker plus `ILogicalTransferTerminalObserver` from the composition root; no universal capacity was introduced. It reserves correlation before submitting the first fragment, registers every accepted fragment, releases on provider rejection/unobservable correlation, retains the generation-safe deferred logical-transfer handle only while correlation remains pending, and subscribes each registered Radio interface to `DeferredLogicalTransferObserverBridge`. Tracker saturation returns explicit `RadioTransportSendStatus::ResourceUnavailable` before any fragment is submitted. `DeferredLogicalTransferDescriptor` retains the originating `RadioPeerHandle` for peer-handle sends so the eventual terminal observation can be correlated upward without recovering raw provider addresses. Synchronously terminal all-fragment evidence emits the same logical terminal contract immediately; deferred provider callbacks aggregate to exactly one later terminal observation. Stale provider handles remain inert.

Mesh has `ForwardingAttemptEvidenceDisposition` plus `ForwardingAttemptLifecycle::AfterRadioTerminalEvidence`. Completed logical Radio transmission—even peer-acknowledged—remains `AwaitingNextHopAcceptance`; terminal Radio failure may drive bounded retry/replanning; deadline dominates; none consumes HopLimit. The next integration is a narrow Mesh observer/correlation surface from RadioTransport logical terminal observations into this already-implemented lifecycle.

Raw80211 esp_wifi_80211_tx success remains submission only. BLE advertising remains queued/submitted bearer work only. NRF24 successful unicast RF24::write proves transmission completion + hardware peer ACK; successful broadcast proves completion with ACK unavailable. Radio peer ACK is still not Mesh next-hop acceptance.

### Clock / traffic / payload
Clock roles elective; quality-first deterministic DeviceIdentifier tie-break. Precision T1/T2/T3/T4 bypass ordinary Mesh queues. ClockCoordinationTable<32> retains authenticated informational advertisements; clock quality/eligibility/root/parent policies injected. Membership gating requires exact current Active incarnation. Unreachable removes informational clock state. Root eligibility and immediate-parent usability are separate: DirectClockParentUsabilityPolicy requires current executable exact-incarnation direct authenticated peer, while elected root may remain non-direct.

Protected traffic capacities InfrastructureResponse8, ClockControl4, GeneralControl8, Application8 through injected governor. Control work finite injected lifetime. Mesh services remain narrow/serialized-domain compatible; no direct FreeRTOS in Mesh. Payload forms Borrowed Stable / Bounded Owned / Repeatable Serialized Source; finite length before acceptance and immutable logical bytes per MessageId.

## Locked capacities
MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; RouteCache32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; Dedup128; same-route attempts3; routes4; traffic8/4/8/8; unreachable retention60s; tombstone retention~5min. Deferred Radio logical-transfer correlation capacity remains explicitly selected by the application/composition root and is not a new locked universal constant.

## Current implementation state

Validated lower/platform: System canonical DeviceIdentifier; Primitive conceptual IDs/version/fingerprint/family boundaries; Radio bounded logical transfer/peer registry/peer-handle unicast/evidence/deferred correlation tracker, observer bridge and RadioTransport fragmentation integration; ESP32 shared PHY + Raw80211 + BLE alignment; NRF24 qualified direct-link evidence; State structural realignment; Command intrinsic RPC removed with naming cleanup remaining; Event provenance/EVTT v2 with distributed identity paused.

Validated Mesh: membership tombstones/dedup/MessageId/inbound reservations; authenticated membership/retention/lifecycle/liveness/probes; bounded pre-auth/authentication; security/admission/promotion; PrimitiveReceiverRegistry<8>; MeshRadioRegistry<4>/discovery/peer cleanup; traffic governor/control lifetime; topology/freshness/publication; routes/cache/strategy/planner/retry; authenticated direct peers; forwarding submission/transition/attempt lifecycle including Radio terminal-evidence classification; delivery ACK/outbound lifecycle; clock coordination/membership/reachability/direct-parent usability; host+ESP32 fixed/cardinality memory accounting.

## Memory accounting
Representative topology type int16 SignalDbm + uint16 CostHint: x86-64 principal stores 37,448 B + DeliveryAcknowledgementTracker<8> 456 B; ESP32 Xtensa principal 37,096 B + ACK(8) 456 B. These are structure measurements, not whole-device runtime RAM. Whole-device budgeting additionally includes stacks, RadioTransport/provider storage, payload/reassembly/control buffers, security private state, application objects and the explicitly selected `DeferredLogicalTransferTracker<N>`. Reference-only formula: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`.

## Recent validation
- Radio `33925240787` green: deferred provider terminal contract plus native/ESP32 smoke.
- Mesh `33925482314` green; Clock Coordination `33925498775` green.
- Mesh Topology Evolution `33953468039` green: forwarding-attempt lifecycle proves deferred Radio completion/ACK remains pending Mesh acceptance and terminal Radio failure may enter retry policy.
- Radio `33953561820`: native job green including deferred logical-transfer observer bridge.
- Radio `33956140263`: first RadioTransport integration validation exposed only a `-Werror=misleading-indentation` formatting defect in the rewritten Stop path; corrected immediately, not treated as behavioral evidence.
- Radio `33956249777`: native job green including existing transport/evidence/deferred/peer/clock regressions and new RadioTransport deferred-evidence integration regression; ESP32-worker compilation still in progress at this handoff write.

## Critical boundaries
Pre-auth cannot mutate authenticated state. Claimed identity is not authenticated identity. Security Pending grants no authority. Radio peer loss is not membership departure. Radio attempt is not completion; Radio completion/peer ACK is not authenticated Mesh next-hop acceptance. Only exact authenticated next-hop acceptance consumes one HopLimit. Next-hop acceptance is not destination ACK; destination ACK is not requested-operation completion. Route/cache is disposable local state. Clock root need not be direct; immediate parent must be current executable exact-incarnation direct authenticated peer. No concrete PrimitiveFamilyId/EventTypeId/Group ID may be invented. Host/ESP32 memory measurements must not be misrepresented as each other or whole-device runtime RAM.

## Immediate next work
1. Confirm Radio run `33956249777` ESP32-worker completion. If green, treat RadioTransport deferred logical-transfer integration as validated on native + target smoke.
2. Connect RadioTransport `ILogicalTransferTerminalObserver` into Mesh's existing `ForwardingAttemptLifecycle::AfterRadioTerminalEvidence` through a narrow bounded correlation/orchestration surface. Radio failure may drive retry/replanning; completion remains pending authenticated next-hop acceptance; no Radio fact consumes HopLimit.
3. Finish Command local-disposition terminology cleanup without compatibility aliases.
4. Continue only frozen Mesh control-plane semantics; stop before concrete Mesh PrimitiveFamilyId or unresolved wire identity. Start MeshAdapters propagation only when adapter implementation requires it. Keep memory accounting current for new bounded retained stores.

## Chronological continuation log
- 2026-09-03: Decisions 1–50D frozen; implementation-readiness audit PASS.
- 2026-09-03–04: System/Primitive/Radio/ESP32/NRF24/State/Command/Event propagation foundations and Mesh membership/admission/liveness/peer/topology/routing/forwarding/delivery implemented incrementally with bounded regressions.
- 2026-09-04: Radio send-attempt/direct-link evidence separated from Mesh next-hop acceptance; outbound delivery/admission lifecycle validated; fixed memory accounting added and ESP32-native measurement validated; clock coordination/membership/reachability/direct-parent usability added.
- 2026-09-04T22:20Z–22:23Z: Radio IRadioSendAttemptObserver, provider-local RadioTransmissionHandle, terminal Failed evidence and IRadioTransmissionObserver implemented; Radio `33925240787` green.
- 2026-09-05T07:44Z–07:46Z: Added Mesh forwarding Radio-terminal evidence classification and lifecycle integration. Completed/peer-ACKed Radio terminal evidence remains pending authenticated next-hop acceptance; failed terminal evidence enters bounded route retry. Topology Evolution `33953468039` green.
- 2026-09-05T07:47Z–07:49Z: Added Radio `DeferredLogicalTransferObserverBridge` + `ILogicalTransferTerminalObserver`, regression for multi-fragment completion, terminal failure and stale handles, umbrella/ESP32 smoke exposure.
- 2026-09-05T08:42Z–08:48Z: Integrated explicit-capacity deferred logical-transfer correlation into RadioTransport. Added pre-fragment reservation, per-fragment registration, interface observer subscriptions, explicit ResourceUnavailable before partial submission, peer-handle retention in terminal descriptor, synchronous/deferred terminal emission, and a saturation/completion/failure/stale-handle regression. First CI found one warning-clean formatting issue; commit `a983d16995ca0ec2a15f41073260d75b326b6d77` corrected it. Radio run `33956249777` native job green; ESP32 worker pending at handoff write.
