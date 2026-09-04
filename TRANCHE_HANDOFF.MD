# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

Validated propagation spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership, admission, topology, routing, forwarding, delivery and target-native memory-accounting foundations. This file is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`.

Do not alter public/distributed semantics, dependency direction, identity/authentication semantics, locked capacities or Policy without Simon Stuart's explicit decision.

**Unresolved public/wire decisions:** concrete ESPressio `PrimitiveFamilyId` allocations; distributed EventTypeId allocation/derivation; conceptual Event MessageId generation scope/strategy; Group identifier contract. Do not invent these.

## Branch scope

Primary:
- `ESPressio-Mesh` → `structural_realignment_propagation`

Affected repositories use `structural_realignment_propagation_ESPressio-Mesh`:
- `ESPressio-System`
- `ESPressio-Primitive`
- `ESPressio-Radio`
- `ESPressio-ESP32`
- `ESPressio-NRF24`
- `ESPressio-State`
- `ESPressio-Command`
- `ESPressio-Event`

`ESPressio-MeshAdapters` is architecturally required but not yet branched/implemented. Repository name is **ESPressio-Primitive singular**.

## Mandatory Policy/workflow rules

- Ordinary libraries never call hardware/platform APIs directly; use ESPressio abstractions.
- Reuse existing abstractions and avoid duplicate responsibilities.
- No circular dependencies, including optional ones. Stop and ask Simon if one becomes unavoidable.
- Participating manifests/tests/jobs/docs/source refs target matching propagation branches.
- Documentation and IDE-compatible doc comments move with APIs.
- Useful public discrete components receive Arduino IDE + PlatformIO demos where appropriate.
- Internal diagnostics use compile-switchable `ESPressio-Logging` after inspecting its current API.
- Optional callback surfaces use `ESPressio-Observable`; optional Event bridges live with the source subsystem.
- Do not change version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.
- Maintain this handoff after meaningful implementation/validation/decision/correction changes. At full completion offer it to Simon for download, then delete it from Git.
- If Actions starts failing immediately due monthly task/job budget exhaustion, record it and stop treating CI as correctness evidence until reset. No such symptom has occurred.

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
- `MeshIdentifier`: exact nonzero 16 B app-supplied.
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

Admission: Unknown → Discovered → Authenticating → Validating → Joining → Active. DeviceIdentifier is never authentication. Direct-neighbour authentication and end-to-end origin authentication are distinct.

`IMeshSecurityAuthority` is Mesh-owned dependency inversion only; candidate context contains generation-safe candidate handle, local RadioIdentifier/RadioPeerHandle and explicitly untrusted identity claim. Only separately established `AuthenticatedMeshIdentity` may enter authenticated membership.

`IMeshAdmissionPolicy` runs only after authentication and performs no cryptography. `AdmissionLifecycleCoordinator` composes bounded candidates/authentication reservations/security/admission/promotion. Pending retains bounded security work; retryable/defer releases expensive work; rejection removes candidate; successful promotion may yield an authenticated direct-peer binding.

Reachability is Unknown / Reachable / Suspect / Unreachable through injected policy; approximate defaults 5 s Suspect / 15 s Unreachable. Valid authenticated evidence may restore Reachable immediately. Full unreachable retention defaults 60 s. Active liveness probes are independently bounded to 4.

Membership tombstones are independently bounded to 64, compact/local, default retention ~5 min, deterministic bounded eviction. Tombstone pressure never weakens authentication/admission.

### Delivery / dedup / acknowledgements

Destinations are Node / Group / CapabilitySelector / Broadcast. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set and normalize to independent Node deliveries. Broadcast is bounded best-effort flood with one MessageId, no frozen recipient set and no complete-delivery promise.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Unseen inbound delivery first reserves bounded InProgress state. Definitive disposition commits dedup; retryable/resource-unavailable releases without commit.

Destination ACK means destination-framework delivery confirmation only. It is distinct from Radio peer ACK, intermediate next-hop acceptance and requested-operation completion. `DeliveryAcknowledgementTracker<Capacity>` requires explicit finite composition capacity; architecture freezes no universal pending-ACK cardinality.

`OutboundDeliveryLifecycle<AcknowledgementCapacity>` composes route-attempt state, exact next-hop forwarding transition, optional final-destination ACK tracking, one MeshMessageId and immutable deadline. It owns no payload, route, queue, task, timer, Radio or wire encoding.

### Topology / routing / forwarding

Topology is a globally bounded directed graph; each member is authoritative only for its own outbound set. A→B and B→A are independent. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide.

No universal `RouteCost` exists. Publication materiality uses injected `ITopologyPublicationPolicy`. Freshness is Fresh / Degraded / Stale / Expired through injected policy; Expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`. `ResolvedRoute<16>` is a local disposable planning artifact, never source-routing wire state. `RouteCache<32>` is optimization only and every route is revalidated. Same-route attempts max 3, distinct routes max 4, under immutable delivery deadline.

`RemainingHopLimit` decrements only after authenticated successful Mesh next-hop forwarding transition. Planning, cache lookup, Radio admission, Radio transmission completion, Radio peer ACK and failed work do not decrement it.

`ForwardingSubmissionCoordinator` resolves a validated next hop through current authenticated membership + exact-incarnation direct-peer binding before RadioTransport send. `ForwardingAttemptLifecycle` keeps accepted Radio work pending exact next-hop Mesh acceptance regardless of direct-link evidence. Only authenticated exact next-hop DeviceIdentifier + MembershipIncarnation + MeshMessageId before deadline can commit the transition.

### Radio / executable peers / evidence

Radio accepts complete immutable bounded logical transfers to directly reachable peers and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is generation-safe process-local link capability only, never Mesh identity.

`AuthenticatedDirectPeerBindingTable<96>` maps authenticated neighbour incarnation + local RadioIdentifier to executable RadioPeerHandle. Peer invalidation removes executable binding only, never membership.

Radio semantics distinguish send attempt/admission, transmission completion and peer acknowledgement. `RadioSendResult` carries qualified `RadioDirectLinkEvidence`; RadioTransport aggregates across fragments. Raw80211 `esp_wifi_80211_tx()` success is submission only. BLE advertising is queued/submitted bearer work only. NRF24 successful unicast `RF24::write()` proves transmission completion + hardware peer ACK; successful broadcast proves transmission completion with ACK unavailable. Even Radio peer ACK is not Mesh next-hop acceptance.

### Clock / traffic / execution / payload

Clock roles are elective. Timing owns math/discipline/quality, Radio owns direct timestamp mechanics, Mesh owns root/parent coordination. Election is quality-first with DeviceIdentifier final deterministic tie-break. Precision T1/T2/T3/T4 bypasses ordinary Mesh queues/fragmentation.

Protected traffic capacities: InfrastructureResponse 8, ClockControl 4, GeneralControl 8, Application 8 through injected governor. Control work has finite injected lifetime. Mesh is decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS belongs in Mesh. All retained resources/queues are finite/backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

### Locked capacities

MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; traffic 8/4/8/8; unreachable full-member retention60s; tombstone retention ~5min.

## Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family-registry boundaries.
- Radio bounded logical transfer, peer registry/lifecycle, peer-handle unicast, direct-link evidence aggregation, corrected send-attempt observer/Event semantics.
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
- compiler/ABI-native fixed/cardinality memory-accounting surface with host and ESP32 validation.

## Memory accounting

`MeshFixedMemoryAccounting<TTopologyCharacteristics>` exposes compiler/ABI-native `sizeof` accounting for the principal frozen-cardinality Mesh stores: authenticated membership, membership liveness, tombstones, inbound delivery reservations, pending neighbour candidates, inbound authentication reservations, liveness probes, authenticated direct-peer bindings, global topology graph, route cache, primitive receiver registry and default traffic governor.

Delivery-acknowledgement bytes are exposed only after the composition root supplies an explicit finite acknowledgement capacity; no universal pending-ACK capacity has been invented.

Representative topology-characteristics test type: `int16_t SignalDbm + uint16_t CostHint`.

Measured results:
- GitHub Actions x86-64 ABI: principal represented stores **37,448 B**; `DeliveryAcknowledgementTracker<8>` **456 B**.
- ESP32 Dev Module / PlatformIO espressif32 7.1.0 / Arduino-ESP32 `3.20017.241212+sha.dcc1105b` / Xtensa ESP32 GCC 8.4.0: principal represented stores **37,096 B**; `DeliveryAcknowledgementTracker<8>` **456 B**.

These are structure/cardinality ABI measurements, **not whole-device runtime RAM budgets**. Whole-device budgeting must still add task stacks, RadioTransport/provider storage, variable payload/reassembly/control buffers, security-authority private state and application objects. The ESP32 probe's aggregate firmware RAM figure is deliberately not used as a Mesh budget because it includes framework runtime and materialized measurement arrays.

Older planning formula remains a reference only: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`.

## Validation record

Recent green evidence:
- Radio `33858866923` — native + ESP32-worker smoke coverage after send-attempt/evidence cleanup.
- Mesh Topology Evolution `33862960895` + General CI `33862960929` — outbound delivery lifecycle.
- Mesh General CI `33863391792` — admission lifecycle broad host suite.
- Mesh Memory Accounting `33870763120` — host accounting regression + complete umbrella; x86-64 37,448 B principal + 456 B ACK(8).
- Mesh Memory Accounting `33885129638` — host accounting plus **ESP32-native probe green**; ESP32 37,096 B principal + 456 B ACK(8).
- Mesh General CI `33885129613` — green against final retained-symbol accounting commit.
- Mesh Topology Evolution `33885129681` — green against final retained-symbol accounting commit.
- Control Work Lifetime `33885129727` — green against final retained-symbol accounting commit.
- All five workflows triggered by commit `2eb1993797748fca10f333e64439eef0fdff014c` concluded successfully.

ESP32 accounting implementation/fix sequence:
- `d7913be53864b8ee501346091db9ada86d8a1118` — PlatformIO probe project.
- `27415ac193232958758da48865293962ece0c67d` — initial target-sized symbols.
- `f3bd737dbc554aeaf50b83a4a9b7cbc6af33ade9` — ESP32 accounting workflow.
- run `33884733576` exposed framework GNU++11 default; no library semantic failure.
- `63f25188ec132572bff42097a6e79f77ea8b6f9d` — explicitly remove C++11 flags and require GNU++17.
- run `33884896907` compiled/linked ESP32 successfully but exposed linker garbage-collection of unreferenced probe symbols.
- `2eb1993797748fca10f333e64439eef0fdff014c` — volatile reads retain exact-sized probe symbols.
- run `33885129638` fully green with Xtensa `nm` extraction and target values above.
- `290d0908675d94a121f4635b7774f1c57dcd1616` — README target-memory documentation.

No Actions-budget symptom has occurred.

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
- No concrete PrimitiveFamilyId/EventTypeId/Group ID may be invented.
- Host ABI memory numbers must never be presented as ESP32 measurements; ESP32 structure measurements must never be presented as whole-device runtime RAM.

## Corrections made during continuation

A temporary `ForwardingEvidencePolicy` and regression were added while inspecting the Radio-evidence boundary. Current-tip reconciliation then showed `ForwardingTransitionCoordinator` already owned the exact required authenticated next-hop success semantics more precisely. The duplicate files were immediately removed. Do **not** recreate them; `ForwardingTransitionCoordinator` / `ForwardingAttemptLifecycle` remain authoritative for successful-hop evidence.

## Immediate next work

1. Finish Command local-disposition terminology cleanup: replace misleading `CommandResult` / `success` / `Ok` / `Error` completion/result vocabulary with names that mean only local handling acceptance/rejection. Update all propagation-branch tests/examples/docs/IDE comments together and do not add compatibility aliases.
2. Continue Mesh control-plane implementation only where frozen architecture supplies enough semantics; stop before concrete Mesh `PrimitiveFamilyId` allocation or any unresolved wire identity.
3. Begin distributed clock coordination (root/parent policies and bounded coordination state) only through already-frozen Timing/Radio/Mesh responsibility split; do not invent a Mesh control-family numeric ID or wire encoding merely to implement the local orchestration state.
4. Start `ESPressio-MeshAdapters` propagation work only when adapter implementation becomes necessary; keep Mesh independent of Command/Event/State.
5. Keep memory accounting current as new bounded retained stores are introduced.

## Chronological continuation log

- 2026-09-03: Architecture Decisions 1–50D frozen; implementation-readiness audit PASS.
- 2026-09-03: System/Primitive/Radio/ESP32/NRF24/State/Command/Event propagation foundations implemented and validated.
- 2026-09-03 to 2026-09-04: Mesh membership, admission, liveness, peer, topology, routing, forwarding and delivery foundations implemented incrementally with bounded regressions.
- 2026-09-04: Radio send-attempt/direct-link evidence semantics explicitly separated from Mesh next-hop acceptance; outbound delivery and admission lifecycle composition validated.
- 2026-09-04T12:02Z: Added `MeshFixedMemoryAccounting<TTopologyCharacteristics>`; x86-64 representative measurement 37,448 B principal + 456 B ACK(8), explicitly non-ESP32.
- 2026-09-04T14:3xZ: Added ESP32 PlatformIO accounting probe and dedicated ESP32 workflow. First attempt exposed GNU++11 framework default; second compiled under GNU++17 but exposed linker GC of unreferenced measurement symbols.
- 2026-09-04T14:40Z–14:42Z: Retained measurement symbols via volatile reads; Memory Accounting run `33885129638` passed with ESP32-native 37,096 B principal + 456 B ACK(8). General CI/Topology Evolution/other triggered workflows also green. README and this authoritative handoff reconciled.
