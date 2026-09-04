# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

Validated propagation spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership, admission, topology, routing, forwarding and delivery foundations. This file is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`.

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
- Internal diagnostics use compile-switchable `ESPressio-Logging` after inspecting its current API.
- Optional callback surfaces use `ESPressio-Observable`; optional Event bridges live with the source subsystem.
- Do not change version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.
- Maintain this handoff after meaningful implementation/validation/decision/correction changes.
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
- `MembershipIncarnation`: exact nonzero 16 B participation-instance ID.
- `MeshNodeAlias`: uint16 Mesh-local routing handle only.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; incarnation-local.
- `MeshMessageId`: uint64 nonzero monotonic/nonwrapping scoped to authenticated `(DeviceIdentifier, MembershipIncarnation)`; retries/routes/fragments retain it.
- `ProfileGeneration` / `TopologyGeneration`: uint64 nonzero.
- `CanonicalName`: mandatory Mesh-unique printable ASCII 1–32 B, exact case-sensitive compare.
- `ContractFingerprint`: exact 32 B SHA-256 semantic compatibility digest, never authentication.
- `PrimitiveFamilyId`: uint16; 0 invalid; 0x0001–0x7FFF ESPressio-defined; 0x8000–0xFFFE private; 0xFFFF reserved. Concrete ESPressio IDs remain unallocated.

### Command / Event / State

Command has no intrinsic wait/acceptance/requested-operation completion/result timeout/destination/reply semantics. Existing `CommandResult` is only a local parser/validation/middleware/dispatch/callback disposition and still has misleading completion-oriented source naming/comments to clean up. Later operation progress/completion belongs to independent Event/State messages, optionally correlated.

Event intrinsic data is EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote origin is dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and Event MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State is application-owned/non-owningly bound; remote replicas are State-owned. Availability is separate from value. Wire concepts are Publication, Availability, Subscribe, Unsubscribe, Resynchronize; no ReplicaAcknowledgement/generic Disconnect.

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

Topology is a globally bounded directed graph; each member is authoritative only for its own outbound set. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide.

No universal `RouteCost` exists. Publication materiality uses injected `ITopologyPublicationPolicy`. Freshness is Fresh / Degraded / Stale / Expired through injected policy; Expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`. `ResolvedRoute<16>` is a local disposable planning artifact, never source-routing wire state. `RouteCache<32>` is optimization only and every route is revalidated. Same-route attempts max 3, distinct routes max 4, under immutable delivery deadline.

`RemainingHopLimit` decrements only after authenticated successful Mesh next-hop forwarding transition. Planning, cache lookup, Radio admission, Radio transmission completion, Radio peer ACK and failed work do not decrement it.

`ForwardingSubmissionCoordinator` resolves a validated next hop through current authenticated membership + exact-incarnation direct-peer binding before RadioTransport send. `ForwardingAttemptLifecycle` keeps accepted Radio work pending exact next-hop Mesh acceptance regardless of direct-link evidence. Only authenticated exact next-hop DeviceIdentifier + MembershipIncarnation + MeshMessageId before deadline can commit the transition.

### Radio / executable peers / evidence

Radio accepts complete immutable bounded logical transfers to directly reachable peers and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is generation-safe process-local link capability only, never Mesh identity.

`AuthenticatedDirectPeerBindingTable<96>` maps authenticated neighbour incarnation + local RadioIdentifier to executable RadioPeerHandle. Peer invalidation removes executable binding only, never membership.

Radio semantics distinguish send attempt/admission, transmission completion and peer acknowledgement. `RadioSendResult` carries qualified `RadioDirectLinkEvidence`; RadioTransport aggregates across fragments. Raw80211 `esp_wifi_80211_tx()` success is submission only. BLE advertising is queued/submitted bearer work only. NRF24 successful unicast `RF24::write()` proves transmission completion + hardware peer ACK; successful broadcast proves transmission completion with ACK unavailable. Even Radio peer ACK is not Mesh next-hop acceptance.

### Traffic / execution / payload

Protected traffic capacities: InfrastructureResponse 8, ClockControl 4, GeneralControl 8, Application 8 through injected governor. Control work has finite injected lifetime. Mesh is decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS belongs in Mesh. All retained resources/queues are finite/backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

Locked capacities: MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; traffic 8/4/8/8.

## Memory accounting — current implementation

`MeshFixedMemoryAccounting<TTopologyCharacteristics>` now exposes compiler/ABI-native `sizeof` accounting for the principal frozen-cardinality Mesh stores:
- authenticated membership;
- membership liveness;
- tombstones;
- inbound delivery reservations;
- pending neighbour candidates;
- inbound authentication reservations;
- liveness probe reservations;
- authenticated direct-peer bindings;
- global topology graph;
- route cache;
- primitive receiver registry;
- default traffic governor.

It deliberately does **not** claim a host x86-64 result is an ESP32 budget. Pointer width/alignment and application-selected topology characteristics are target-specific. Delivery-acknowledgement bytes are exposed only after the composition root supplies an explicit finite acknowledgement capacity.

Whole-device budgeting must still add task stacks, RadioTransport/provider storage, variable payload/reassembly/control buffers, security-authority private state and application objects. The older provisional formula remains a planning reference only: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`.

On GitHub Actions x86-64, the current representative test topology-characteristics type reports `PrincipalFixedCardinalityBytes = 37,448` and `DeliveryAcknowledgementTracker<8> = 456` bytes. These numbers are regression evidence only and must not be presented as ESP32 measurements.

## Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family-registry boundaries.
- Radio bounded logical transfer, peer registry/lifecycle, peer-handle unicast, direct-link evidence aggregation, corrected send-attempt observer/Event semantics.
- ESP32 shared Wi-Fi/Raw80211 PHY coordination, Raw80211 submission-only evidence, BLE Radio alignment.
- NRF24 direct-link semantics.
- State structural realignment.
- Command intrinsic RPC machinery removed and docs/Wiki realigned; local disposition naming/comment cleanup remains.
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
- target-native fixed/cardinality memory-accounting surface.

## Validation record

Recent green evidence:
- Radio `33858866923` — native + ESP32-worker smoke coverage after send-attempt/evidence cleanup.
- Mesh Topology Evolution `33862960895` + General CI `33862960929` — outbound delivery lifecycle.
- Mesh General CI `33863391792` — admission lifecycle broad host suite.
- Mesh Memory Accounting **`33870763120`** — new accounting regression + complete Mesh umbrella build; x86-64 representative output 37,448 principal fixed-cardinality bytes and 456 bytes for ACK tracker capacity 8.

Memory-accounting commits:
- `6838147938824dd7e48e36d8b09d4efbd9c26dc0` — initial target-native accounting surface.
- `6a8abff245123ee078e4f9e40093fdb7344d8b0b` — explicit acknowledgement dependency/accounting fix.
- `d6a01da87824f9c045da35dcbccaaa5fdfe7f86c` — accounting regression.
- `13952398a4fac261f7f83a59aa600f5ee39162c2` — umbrella exposure.
- `4239290c12493134135a33af3b5f29a1428b29a9` — dedicated validation workflow.
- `053a5f00acfcbbec58c4446f739f148834f55bed` — README memory-accounting documentation.

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

## Immediate next work

1. Finish Command local-disposition terminology cleanup: replace misleading `CommandResult` / `success` / `Ok` / `Error` completion/result vocabulary with names that mean only local handling acceptance/rejection. Update tests/examples/docs/IDE comments together; do not reintroduce RPC semantics or legacy aliases.
2. Extend memory accounting on an actual ESP32 compile target so the target compiler can expose ABI-correct component sizes without pretending host numbers are device numbers. Do not lock payload/control/reassembly/ACK capacities merely to obtain a total.
3. Continue Mesh control-plane implementation only where frozen architecture supplies enough semantics; stop before concrete Mesh PrimitiveFamilyId allocation or any unresolved wire identity.
4. Start `ESPressio-MeshAdapters` propagation work only when adapter implementation becomes necessary; keep Mesh independent of Command/Event/State.

## Chronological continuation log

- 2026-09-03: Architecture Decisions 1–50D frozen; implementation-readiness audit PASS.
- 2026-09-03: System/Primitive/Radio/ESP32/NRF24/State/Command/Event propagation foundations implemented and validated.
- 2026-09-03 to 2026-09-04: Mesh membership, admission, liveness, peer, topology, routing, forwarding and delivery foundations implemented incrementally with bounded regressions.
- 2026-09-04: Radio send-attempt/direct-link evidence semantics explicitly separated from Mesh next-hop acceptance.
- 2026-09-04: Outbound delivery and admission lifecycle composition validated.
- 2026-09-04T12:02Z: Added `MeshFixedMemoryAccounting<TTopologyCharacteristics>`, target-native accounting regression and dedicated workflow. Host x86-64 regression measured 37,448 B for represented principal fixed/cardinality stores and 456 B for ACK capacity 8; explicitly recorded as non-ESP32 evidence only. README updated accordingly.
