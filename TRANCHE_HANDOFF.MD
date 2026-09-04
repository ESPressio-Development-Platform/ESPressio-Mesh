# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

This is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`. Validated propagation now spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership, admission, topology, routing, forwarding, delivery, clock coordination and target-native memory-accounting foundations.

Do not alter public/distributed semantics, dependency direction, identity/authentication semantics, locked capacities or Policy without Simon Stuart's explicit decision.

### Unresolved public/wire decisions

Do **not** invent any of these:
- concrete ESPressio `PrimitiveFamilyId` allocations;
- distributed EventTypeId allocation/derivation;
- conceptual Event MessageId generation scope/strategy;
- Group identifier contract.

## Branch scope

Primary:
- `ESPressio-Mesh` → `structural_realignment_propagation`

Affected repositories use `structural_realignment_propagation_ESPressio-Mesh`:
- ESPressio-System
- ESPressio-Primitive **(singular)**
- ESPressio-Radio
- ESPressio-ESP32
- ESPressio-NRF24
- ESPressio-State
- ESPressio-Command
- ESPressio-Event

`ESPressio-MeshAdapters` is architecturally required but has not yet needed a propagation branch/implementation.

## Mandatory Policy/workflow rules

- Ordinary libraries do not call hardware/platform APIs directly; use ESPressio-System abstractions. Hardware concretes belong in platform/technology repositories.
- Reuse existing abstractions; preserve strict SRP and dependency direction.
- No circular dependency, including optional dependency. Stop and ask Simon if one becomes unavoidable.
- Participating manifests/tests/jobs/docs/source references target matching propagation branches.
- Documentation and IDE-compatible doc comments move with APIs.
- Useful public discrete components receive Arduino IDE + PlatformIO demos where appropriate.
- Internal diagnostics use compile-switchable ESPressio-Logging after inspecting its current API.
- Optional callback surfaces use ESPressio-Observable; optional Event bridges live with the source subsystem.
- Do not alter version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.
- Maintain this file after meaningful implementation, validation, decision or correction changes. At full tranche completion offer the handoff to Simon for download, then remove it from Git.
- If Actions begins failing immediately because of monthly task/job budget exhaustion, record that fact and stop treating CI as correctness evidence until reset. No such symptom has occurred.

## Frozen architecture

### Layering

- System: foundational platform-neutral identity/types/services; canonical `DeviceIdentifier`.
- Primitive: dependency-neutral conceptual-message vocabulary only.
- Command: asynchronous intent, never RPC.
- Event: occurrence semantics.
- State: authoritative fact/replication.
- Radio: opaque direct-link physical/link transport and hop-local packetization/reassembly/retry/evidence.
- Mesh: membership/topology/routing/control plane/logical addressing/admission/multi-radio forwarding/end-to-end delivery. Mesh must not depend on Command/Event/State.
- MeshAdapters: optional integration above Mesh and Command/Event/State.
- Timing: synchronization math/discipline/quality; Mesh coordinates root/parent; Radio owns direct timestamp mechanics.

### Identity and primitive vocabulary

- `DeviceIdentifier`: exact 16 B, permanent transport-independent, zero invalid, never authentication.
- `MeshIdentifier`: exact nonzero 16 B application-supplied.
- `MembershipIncarnation`: exact nonzero 16 B participation-instance ID; partition/Radio restart does not inherently change it.
- `MeshNodeAlias`: uint16 Mesh-local routing handle only.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; incarnation-local and not recycled within one incarnation.
- `MeshMessageId`: uint64 nonzero monotonic/nonwrapping scoped to authenticated `(DeviceIdentifier, MembershipIncarnation)`; retries/routes/fragments retain it; each selective-recipient delivery receives one; Broadcast retains one throughout flood.
- `ProfileGeneration` / `TopologyGeneration`: uint64 nonzero.
- `CanonicalName`: mandatory Mesh-unique printable ASCII 1–32 B, exact case-sensitive compare, no leading/trailing space.
- `ContractFingerprint`: exact 32 B SHA-256 semantic compatibility digest, never authentication.
- `PrimitiveFamilyId`: uint16; 0 invalid; 0x0001–0x7FFF ESPressio-defined; 0x8000–0xFFFE private; 0xFFFF reserved. Concrete ESPressio IDs remain unallocated.

### Command / Event / State

Command has no intrinsic wait/acceptance/requested-operation completion/result timeout/destination/reply semantics. Existing `CommandResult` is only a local parser/validation/middleware/dispatch/callback disposition and still has misleading completion-oriented source naming to clean up. Later operation progress/completion belongs to independent Event/State messages, optionally correlated.

Event intrinsic data is EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote origin is dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and Event MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State is application-owned/non-owningly bound; remote replicas are State-owned. Availability is separate from value. Wire concepts are Publication, Availability, Subscribe, Unsubscribe, Resynchronize; no ReplicaAcknowledgement/generic Disconnect. StateEpoch is independent from MembershipIncarnation.

### Membership / admission / liveness

Admission is Unknown → Discovered → Authenticating → Validating → Joining → Active. DeviceIdentifier is never authentication. Direct-neighbour authentication and end-to-end origin authentication are distinct.

`IMeshSecurityAuthority` is Mesh-owned dependency inversion only. Candidate context contains a generation-safe candidate handle, local RadioIdentifier/RadioPeerHandle and explicitly untrusted identity claim. Only separately established `AuthenticatedMeshIdentity` may enter authenticated membership. `IMeshAdmissionPolicy` runs only after authentication and performs no cryptography. `AdmissionLifecycleCoordinator` composes bounded candidates/authentication reservations/security/admission/promotion.

Reachability is Unknown / Reachable / Suspect / Unreachable through injected policy; approximate defaults 5 s Suspect / 15 s Unreachable. Valid authenticated evidence may restore Reachable immediately. Full unreachable retention defaults 60 s. Active liveness probes are independently bounded to 4. Membership tombstones are independently bounded to 64 with ~5 min default retention and deterministic bounded eviction; tombstone pressure never weakens authentication/admission.

### Delivery / routing / forwarding

Destinations are Node / Group / CapabilitySelector / Broadcast. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set and normalize to independent Node deliveries. Broadcast is bounded best-effort flood with one MessageId, no frozen recipient set and no complete-delivery promise.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Definitive inbound disposition commits dedup; retryable/resource-unavailable releases InProgress state without commit.

Destination ACK means destination-framework delivery confirmation only. It is distinct from Radio peer ACK, intermediate next-hop acceptance and requested-operation completion. `DeliveryAcknowledgementTracker<Capacity>` requires explicit finite composition capacity; no universal pending-ACK cardinality is frozen.

Topology is a globally bounded directed graph. Each member is authoritative only for its own outbound set. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide. No universal `RouteCost` exists. Publication materiality and freshness are policy-injected; Expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`. `ResolvedRoute<16>` is a disposable local planning artifact, never source-routing wire state. `RouteCache<32>` is optimization only and routes are revalidated. Same-route attempts max 3, distinct routes max 4, under immutable delivery deadline.

`RemainingHopLimit` decrements only after authenticated successful Mesh next-hop forwarding transition. Planning, cache lookup, Radio admission, Radio transmission completion, Radio peer ACK and failed work do not decrement it. `ForwardingSubmissionCoordinator` resolves a validated next hop through current authenticated membership + exact-incarnation direct-peer binding before RadioTransport send. `ForwardingAttemptLifecycle` remains pending until exact authenticated next-hop Mesh acceptance.

### Radio / executable peers / evidence

Radio accepts complete immutable bounded logical transfers to directly reachable peers and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is generation-safe process-local link capability only, never Mesh identity. `AuthenticatedDirectPeerBindingTable<96>` maps authenticated neighbour incarnation + local RadioIdentifier to executable RadioPeerHandle. Peer invalidation removes executable binding only, never membership.

Radio semantics distinguish synchronous send attempt/admission, transmission completion and peer acknowledgement. `RadioSendResult` carries qualified `RadioDirectLinkEvidence`; RadioTransport aggregates across fragments. Raw80211 `esp_wifi_80211_tx()` success is submission only. BLE advertising is queued/submitted bearer work only. NRF24 successful unicast `RF24::write()` proves transmission completion + hardware peer ACK; successful broadcast proves transmission completion with ACK unavailable. Even Radio peer ACK is not Mesh next-hop acceptance.

### Clock coordination

Clock roles are elective. Timing owns math/discipline/quality, Radio owns direct timestamp mechanics, Mesh owns root/parent coordination. Election is quality-first with DeviceIdentifier final deterministic tie-break. Precision T1/T2/T3/T4 bypass ordinary Mesh queues/fragmentation.

`ClockCoordinationTable<32>` retains authenticated informational advertisements. `IClockQualityPolicy`, `IClockEligibilityPolicy`, `IClockRootElectionPolicy` and `IClockParentSelectionPolicy` are injected. `ClockMembershipCoordinator` gates observations to the exact current **Active** authenticated membership incarnation and performs exact-incarnation retirement cleanup.

`ClockReachabilityCoordinator` now converges retained clock state with authenticated reachability: Reachable and Suspect remain retained for injected policy to judge; exact current Unreachable membership loses its informational clock advertisement. This coordinator does not change membership, authenticate messages, choose root/parent, schedule exchanges or discipline Timing. A later authenticated advertisement may participate again after liveness restores Reachable.

No Mesh control-family numeric ID, clock-advertisement wire encoding, Timing discipline algorithm or universal quality metric has been invented.

### Traffic / execution / payload

Protected traffic capacities: InfrastructureResponse 8, ClockControl 4, GeneralControl 8, Application 8 through injected governor. Control work has finite injected lifetime. Mesh is decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS belongs in Mesh. All retained resources/queues are finite/backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size is known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

## Locked capacities

MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; traffic 8/4/8/8; unreachable full-member retention60s; tombstone retention ~5min.

## Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family-registry boundaries.
- Radio bounded logical transfer, peer registry/lifecycle, peer-handle unicast, direct-link evidence aggregation and corrected send-attempt semantics.
- ESP32 shared Wi-Fi/Raw80211 PHY coordination, Raw80211 submission-only evidence and BLE Radio alignment.
- NRF24 direct-link semantics and qualified synchronous completion/ACK evidence.
- State structural realignment.
- Command intrinsic RPC machinery removed and docs/Wiki realigned; local disposition naming cleanup remains.
- Event provenance/context separation and EVTT v2; distributed identity paused.

Validated Mesh foundations:
- membership tombstones/dedup/MessageId/inbound reservations;
- authenticated membership/retention/lifecycle/liveness/probes;
- bounded pre-auth candidate/authentication resources;
- security/admission/promotion/lifecycle;
- PrimitiveReceiverRegistry<8> and advertised support snapshot;
- MeshRadioRegistry<4>, neighbour discovery and peer cleanup;
- traffic governor/control-work lifetime;
- directed topology, graph-wide storage, freshness/publication policy;
- ResolvedRoute<16>, RouteCache<32>, injected routing/revalidation/planner;
- route-attempt/retry policy and deadline-aware coordinator;
- authenticated direct-peer binding/lifecycle;
- forwarding submission, exact authenticated next-hop transition and forwarding-attempt lifecycle;
- delivery ACK tracker/coordinator and outbound-delivery lifecycle;
- bounded policy-injected clock root/parent coordination, exact-Active membership gating and Unreachable-state convergence;
- compiler/ABI-native fixed/cardinality memory accounting with host and ESP32 validation.

## Memory accounting

`MeshFixedMemoryAccounting<TTopologyCharacteristics>` accounts the principal frozen-cardinality Mesh stores. Delivery-acknowledgement bytes are exposed only after the composition root supplies an explicit finite capacity.

Representative topology-characteristics test type: `int16_t SignalDbm + uint16_t CostHint`.

Measured structure/cardinality results:
- GitHub Actions x86-64 ABI: principal represented stores **37,448 B**; `DeliveryAcknowledgementTracker<8>` **456 B**.
- ESP32 Dev Module / PlatformIO espressif32 7.1.0 / Arduino-ESP32 `3.20017.241212+sha.dcc1105b` / Xtensa ESP32 GCC 8.4.0: principal represented stores **37,096 B**; `DeliveryAcknowledgementTracker<8>` **456 B**.

These are not whole-device runtime RAM budgets. Whole-device budgeting must additionally include task stacks, RadioTransport/provider storage, variable payload/reassembly/control buffers, security-authority private state and application objects. Older planning formula remains reference-only: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`.

## Recent validation record

- Radio `33858866923` — native + ESP32-worker smoke after send-attempt/evidence cleanup.
- Mesh Topology Evolution `33862960895` + General CI `33862960929` — outbound delivery lifecycle.
- Mesh General CI `33863391792` — admission lifecycle broad host suite.
- Mesh Memory Accounting `33885129638` — host + ESP32-native accounting; ESP32 37,096 B principal + 456 B ACK(8).
- Mesh General CI `33885129613`, Topology Evolution `33885129681`, Control Work Lifetime `33885129727` — green against retained-symbol accounting tip.
- Clock Coordination `33887726664` — bounded root/parent policy and coordination table.
- Clock Coordination `33888165299` — exact Active membership gating, stale-incarnation rejection and retirement cleanup.
- Clock Coordination `33921807775` — failed because an existing clock-membership regression used unqualified sibling `System::` namespace; this was a test-source defect, not a Mesh semantic failure. Corrected immediately.
- Clock Coordination **`33921907577` — green**, including base clock coordination, authenticated membership gating and new reachability convergence regression.

## Critical boundaries

- Pre-auth state cannot mutate authenticated membership/dedup/liveness.
- Candidate identity claim is never authenticated identity.
- Security Pending retains bounded work but grants no authority.
- Admission policy runs only after authentication.
- Radio peer loss never implies membership departure.
- Direct executable peer binding is exact-incarnation-qualified and local only.
- Radio send attempt is not transmission completion unless qualified evidence says so.
- Radio completion/peer ACK are not authenticated next-hop Mesh acceptance.
- Only authenticated exact next-hop Mesh acceptance may consume one HopLimit.
- Next-hop acceptance is not end-to-end destination ACK.
- Destination ACK is not requested-operation completion.
- Route/cache state is disposable local optimization and never distributed source-routing authority.
- Clock advertisements are informational and may affect local election only after exact current Active membership authority; Unreachable members cannot retain clock-election state.
- No concrete PrimitiveFamilyId/EventTypeId/Group ID may be invented.
- Host ABI memory numbers must never be presented as ESP32 measurements; ESP32 structure measurements must never be presented as whole-device runtime RAM.

## Immediate next work

1. Finish Command local-disposition terminology cleanup: replace misleading `CommandResult` / `success` / `Ok` / `Error` completion/result vocabulary with names that mean only local handling acceptance/rejection. Update propagation-branch tests/examples/docs/IDE comments together and do not add compatibility aliases.
2. Continue clock orchestration only where semantics remain wire-neutral and frozen. The next useful boundary is parent usability: a selected synchronization parent must be currently usable as a direct authenticated neighbour, without incorrectly requiring the elected root itself to be direct. Do not collapse root eligibility and parent eligibility into one policy.
3. Continue Mesh control-plane implementation only where frozen architecture supplies enough semantics; stop before concrete Mesh `PrimitiveFamilyId` allocation or unresolved wire identity.
4. Start ESPressio-MeshAdapters propagation work only when adapter implementation becomes necessary; keep Mesh independent of Command/Event/State.
5. Keep memory accounting current when new bounded retained stores are introduced.

## Chronological continuation log

- 2026-09-03: Architecture Decisions 1–50D frozen; implementation-readiness audit PASS.
- 2026-09-03 to 2026-09-04: System/Primitive/Radio/ESP32/NRF24/State/Command/Event propagation foundations plus Mesh membership, admission, liveness, peer, topology, routing, forwarding and delivery foundations implemented incrementally with bounded regressions.
- 2026-09-04: Radio send-attempt/direct-link evidence semantics explicitly separated from Mesh next-hop acceptance; outbound delivery and admission lifecycle composition validated.
- 2026-09-04T12:02Z: Added Mesh fixed/cardinality memory accounting; x86-64 representative measurement 37,448 B principal + 456 B ACK(8), explicitly non-ESP32.
- 2026-09-04T14:40Z–14:42Z: ESP32-native accounting validated at 37,096 B principal + 456 B ACK(8); README/handoff reconciled.
- 2026-09-04T14:55Z: Added bounded wire-neutral ClockCoordinationTable<32> and injected clock policies; run `33887726664` green.
- 2026-09-04T16:1xZ: Added ClockMembershipCoordinator exact-Active membership gating and exact-incarnation cleanup; run `33888165299` green.
- 2026-09-04T21:35Z: Added ClockReachabilityCoordinator. Initial expanded workflow exposed an existing test namespace defect; corrected both clock regressions. Run `33921907577` green with Reachable/Suspect retention, exact Unreachable removal, stale-incarnation protection and later Reachable re-entry validated.
