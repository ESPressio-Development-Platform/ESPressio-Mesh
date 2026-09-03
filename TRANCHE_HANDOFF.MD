# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE PASS / IMPLEMENTATION READY. SYSTEM / PRIMITIVE / RADIO / ESP32 / NRF24 / STATE / COMMAND RPC-REMOVAL / EVENT PROVENANCE VALIDATED. MESH MEMBERSHIP, DELIVERY/DEDUP, RETENTION, LIVENESS, PRE-AUTH ADMISSION, AUTHENTICATED PROMOTION, RADIO PEER/DISCOVERY LIFECYCLE, EXTERNAL PRIMITIVE RECEIVERS, DETERMINISTIC ADVERTISED PRIMITIVE-SUPPORT SNAPSHOTS, PROTECTED TRAFFIC GOVERNANCE, FINITE CONTROL-WORK LIFETIME, POLICY-DRIVEN ACTIVE LIVENESS PROBING, BOUNDED DIRECTED TOPOLOGY, POLICY-DRIVEN TOPOLOGY FRESHNESS/PUBLICATION, GLOBALLY BOUNDED GRAPH STORAGE, BOUNDED LOCAL ROUTE-PLANNING/CACHE, INJECTED ROUTING STRATEGY, CACHE REVALIDATION, AND BOUNDED DEADLINE-AWARE ROUTE-ATTEMPT ORCHESTRATION ARE VALIDATED.**

This file is the authoritative implementation-tranche handoff for the tranche originating in `ESPressio-Mesh`. The architecture below is frozen. Implementation may choose bounded internal mechanics only where they do not alter public/distributed semantics, ownership, dependency direction, wire compatibility, authentication/identity semantics, locked bounds or Policy. If a genuine architectural omission/conflict is exposed, stop and ask Simon Stuart.

**Explicit unresolved public/wire decision boundaries:** concrete ESPressio-defined `PrimitiveFamilyId` allocations; distributed EventTypeId assignment; conceptual Event message-ID generation scope; Group identifier contract. Do not invent any of them.

---

# 1. Scope and branches

Primary:
- `ESPressio-Mesh`: `structural_realignment_propagation`

Affected propagation branches:
- `ESPressio-System`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Primitive`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Radio`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-ESP32`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-NRF24`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-State`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Command`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Event`: `structural_realignment_propagation_ESPressio-Mesh`

`ESPressio-MeshAdapters` remains architecturally required but is not yet implemented/branched in this tranche. Create/verify its matching propagation branch when adapter work starts.

Authoritative correction from Simon: repository name is **`ESPressio-Primitive` (singular)**. Policy references to `ESPressio-Primitives` are typos.

---

# 2. Mandatory Policy rules

- Ordinary libraries must not call platform/hardware APIs directly. Use the correct ESPressio abstraction; hardware concretes belong in platform/technology repositories.
- Reuse existing ESPressio abstractions rather than duplicating them.
- Avoid every circular dependency, including optional ones. If one appears necessary, stop and ask Simon.
- Keep cross-library propagation isolated to the branches above. Participating manifests, jobs/tests, examples, docs and source references must target matching propagation branches.
- Add useful Arduino IDE and PlatformIO demos for public-facing discrete components where appropriate.
- Keep documentation and IDE-compatible doc comments synchronized with APIs.
- If Actions starts failing almost immediately because the monthly task/job budget is exhausted, explicitly record it and stop treating CI as correctness evidence until reset; switch to recursive self-review. **No budget-exhaustion symptom has occurred.**
- Internal diagnostics use `ESPressio-Logging` and remain compile-switchable; inspect current Logging API before instrumentation.
- Optional callback surfaces use `ESPressio-Observable` multi-observer semantics; optional Observable→Event bridges live with their source subsystem.
- Maintain this `TRANCHE_HANDOFF.MD` on the operative Mesh branch while incomplete and update it after meaningful code/branch/dependency/validation/decision/correction changes. At full completion offer the final handoff to Simon for download, then delete it from Git.
- Do not change version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.

---

# 3. Frozen architecture

## 3.1 Layering

- `ESPressio-System`: foundational platform-neutral identities/types/services; owns canonical `DeviceIdentifier`.
- `ESPressio-Primitive`: dependency-neutral conceptual-message vocabulary only (`ConceptualMessageId`, `CorrelationId`, `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, `ContractFingerprint`, minimal helpers/contracts).
- `ESPressio-Command`: asynchronous intent, not RPC.
- `ESPressio-Event`: occurrence semantics.
- `ESPressio-State`: authoritative fact/replication semantics.
- `ESPressio-Radio`: opaque direct-link physical/link transport and link-local mechanics.
- `ESPressio-Mesh`: membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end delivery. Mesh must not depend on Command/Event/State.
- `ESPressio-MeshAdapters`: optional Mesh↔Command/Event/State integration above both sides.
- `ESPressio-Timing`: synchronization mathematics/discipline/quality; Mesh owns distributed root/parent coordination, Radio direct-link timestamp mechanics.

## 3.2 Identity and fixed types

- `DeviceIdentifier`: exact 16 B, nonzero valid, System-owned, permanent/transport-independent, never authentication.
- `MeshIdentifier`: exact nonzero 16 B application-supplied Mesh identity.
- `MembershipIncarnation`: exact nonzero 16 B participation-instance identity; partition/Radio restart does not inherently change it. FastRejoin may retain it only with authenticated continuity evidence.
- `MeshNodeAlias`: uint16 Mesh-local routing handle only.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; node-local and unrecycled during one incarnation.
- `MeshMessageId`: uint64, nonzero, monotonic/nonwrapping, scoped by authenticated source DeviceIdentifier + MembershipIncarnation. Retries/routes/fragments retain it; selective-multicast recipients each get one; Broadcast retains one throughout flood.
- `ProfileGeneration`, `TopologyGeneration`: uint64 nonzero, incarnation-scoped, begin at 1 and never wrap.
- `CapabilityMask`: default uint64.
- `CanonicalName`: mandatory Mesh-unique profile property; 1–32 printable ASCII bytes, no NUL/control, no leading/trailing spaces, exact case-sensitive comparison; semantic storage one length byte + 32 backing bytes.
- `ContractFingerprint`: exact 32-B SHA-256 semantic compatibility digest, never authentication.

## 3.3 Primitive family vocabulary

`PrimitiveFamilyId` is exact uint16: `0x0000` invalid; `0x0001–0x7FFF` ESPressio-defined and individually allocated centrally; `0x8000–0xFFFE` application/private; `0xFFFF` reserved. Version compatibility uses bounded inclusive ranges and normally selects the highest mutual version. Supports / Compatible / Reachable / Authorized are distinct. **No concrete ESPressio family IDs have been allocated in this tranche.**

## 3.4 Command / Event / State

Command represents asynchronous intent. It has no intrinsic waiting, acceptance response, requested-operation completion/result, timeout, destination/route or reply semantics. Local handler/registry disposition means only what the local framework did with the invocation.

Event represents an occurrence. It contains no transport address or HopCount. Local/Remote origin belongs to `EventDispatchContext`, not Event object state. EventPriority is a local/application dispatch hint, not Mesh QoS. Event is one-way and has no intrinsic ACK/response/completion/RPC contract. Frozen intrinsic metadata is EventTypeId, SchemaVersion, conceptual MessageId, EventPriority, payload and optional CorrelationId. Existing stable-name FNV transport TypeID is not automatically the distributed EventTypeId.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State is application-owned and non-owningly bound/indexed/observed; remote replicas are State-owned. State is read-only outside authority. Availability is separate from value. Protocol concepts are Publication, Availability, Subscribe, Unsubscribe and Resynchronize; no ReplicaAcknowledgement or generic Disconnect. StateEpoch is independent from MembershipIncarnation. MeshAdapters validates StateAddress.DeviceIdentifier against authenticated Mesh source.

## 3.5 Primitive receivers / NodeProfile

Mesh has its own control Primitive family; its numeric family ID remains unallocated. External families remain opaque and terminate in exactly one active receiver per PrimitiveFamilyId. Unsupported family is local disposition and does not negate successful Mesh delivery.

Authenticated NodeProfile ultimately contains DeviceIdentifier, CanonicalName, CapabilityMask, bounded Groups, primitive support descriptors and ProfileGeneration. Primitive support descriptors carry family ID, supported version range and fingerprint. Full NodeProfile remains blocked by the unresolved Group identifier contract.

## 3.6 Membership, admission, security and liveness

Admission progression: Unknown → Discovered → Authenticating → Validating → Joining → Active. Cheap compatibility checks precede expensive crypto. Any Active member may sponsor; sponsor is not leader. MembershipState and ReachabilityState are distinct. Authenticated Leave is authoritative; reachability expiry is local evidence only. DeviceIdentifier is not authentication. Old-incarnation traffic cannot mutate current authority/dedup state.

Security/admission remain behind injected abstractions such as `IMeshSecurityAuthority` / `IMeshAdmissionPolicy`; Mesh itself must not implement cryptography. Direct-neighbour authentication and end-to-end origin authentication are distinct. Security contexts may optimize verification but never redefine identity; they are bounded and scoped to authenticated participation incarnations. Every forwarding node validates the configured security/integrity contract before forwarding. Authentication/integrity and confidentiality are separate properties.

Reachability is Unknown / Reachable / Suspect / Unreachable via injected liveness policy. Approximate baseline floors are ~5 s to Suspect and ~15 s to Unreachable, not universal constants. Valid authenticated evidence may restore Reachable immediately. Active probes are used only when passive/control evidence is insufficient; probe eligibility/cadence is injectable and active probe capacity is bounded to 4.

## 3.7 Tombstones / delivery / dedup

`MaxMembershipTombstones=64`, independent from `MaxMeshNodes=32`. Tombstones retain only DeviceIdentifier, MembershipIncarnation, local monotonic deadline and disposition LocallyForgotten / SupersededIncarnation / AuthoritativeLeave. Default retention ~5 min with deterministic bounded eviction; tombstone exhaustion never weakens auth/admission.

API destinations are Node, Group, CapabilitySelector and Broadcast. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set and normalize into independent Node deliveries. Broadcast is a bounded best-effort flood with one MeshMessageId, no frozen recipient set and no promise of delivery to every member. Applications requiring per-member delivery knowledge use selective multicast rather than reliable Broadcast.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Unseen delivery first reserves bounded InProgress; definitive disposition commits dedup, while TemporarilyUnavailable/ResourceUnavailable releases without commit. Old-incarnation traffic never enters current dedup state.

## 3.8 Topology, routing and retries

Topology and routing are distinct. Topology is a bounded full known directed link-state graph; each member is authoritative only for its own outbound observations. A→B and B→A are independent. Logical edge identity contains advertising DeviceIdentifier, local RadioIdentifier, neighbour DeviceIdentifier and neighbour RadioIdentifier/link endpoint where knowable. RadioAddress and RadioPeerHandle are never distributed topology identity.

`TopologyGeneration` is nonzero uint64 scoped to DeviceIdentifier + MembershipIncarnation and versions one complete published outbound-link set. Material semantic changes advance it; retransmission does not. A newer complete generation replaces the older set, with removals represented by absence. ProfileGeneration and TopologyGeneration are independent.

Topology stores bounded technology-independent observations, not a universal scalar route cost. Normalized observations may represent latency, reliability, capacity, pressure, energy, freshness/confidence etc. with explicit availability semantics. `IRoutingStrategy` interprets observations. Prefer fixed-width/fixed-point representation over float/double.

Publication materiality is separately governed by injectable `ITopologyPublicationPolicy`; raw samples do not automatically advance TopologyGeneration. Remote topology freshness is independent from generation and is Fresh / Degraded / Stale / Expired. It uses local monotonic receipt age, expected cadence, authority reachability and relevant link characteristics/confidence via injectable `ITopologyFreshnessPolicy`. Authenticated retransmission of the same semantic generation may refresh freshness without advancing generation. New authenticated MembershipIncarnation immediately invalidates old-incarnation topology for routing. Expired topology is never used to originate a new route.

Mesh owns routing orchestration; Radio executes selected next-hop work. Routing/attempt/retry behavior is injectable (`IRoutingStrategy`, `IRouteAttemptPolicy`, `IRetryPolicy`). Default bounds: MaxTopologyLinks 96, RouteCache 32, MaxRouteHops 16, DefaultHopLimit 16, max 3 same-route attempts and 4 distinct routes. Forwarding is adaptive hop-by-hop and RemainingHopLimit decrements only on a successful Mesh forwarding transition. Route cache is optimization only and every cache hit is revalidated against present membership/freshness/link/policy evidence before use. The immutable absolute delivery deadline is the outer bound over all route attempts and re-plans.

## 3.9 Radio / ESP32 shared PHY

Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer/handle. MaximumLogicalTransferSize is finite and distinct from physical MTU. Radio owns bounded fragmentation/reassembly and physical retry. Receive yields complete opaque logical transfer plus Radio-owned peer/address observations and qualified optional timing metadata. Radio never interprets DeviceIdentifier, MembershipIncarnation, MeshMessageId, PrimitiveFamilyId or Mesh routing/destination semantics.

Radio may create generation-safe process-local peer handles before authentication. Mesh binds them beside its incarnation-scoped RadioIdentifier; peer handles never become Mesh identity and are never distributed/persisted as identity. Loss of an executable Radio peer binding is link-local evidence only and does not terminate authenticated membership.

ESP32 ordinary Wi-Fi and Raw80211 share one integrated PHY coordinated by ESPressio-ESP32. Raw follows effective Wi-Fi channel by default and must never disruptively retune an incompatible active service. Channel/tx-power/power-save global state belongs to the ESP32 coordinator; Mesh sees only link/topology availability changes.

## 3.10 Clock, traffic, execution, payload and policy signature

Clock roles are elective. Timing owns math/discipline/quality; Radio owns direct timestamp mechanics; Mesh owns root/parent coordination. Election is quality-first with DeviceIdentifier final deterministic tie-break. Precision T1/T2/T3/T4 bypasses ordinary Mesh queues/fragmentation.

`IMeshTrafficGovernor` is injected. Default independently protected capacities: InfrastructureResponse=8, ClockControl=4, GeneralControl=8, Application=8. Application saturation cannot consume control reserves. `IControlWorkLifetimePolicy` gives control work finite lifetime.

Mesh decomposes into narrow services; responsibility does not imply a task. Prefer ESPressio Task/PrecisionThread; no direct FreeRTOS in Mesh. One serialized Mesh execution domain is acceptable. Every queue/resource remains finite and exposes backpressure.

Payload forms: Borrowed Stable Payload / Bounded Owned Payload / Repeatable Serialized Source. Finite size is known before acceptance and one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

Distributed-authority-changing policy/config contributes recursively to MeshSignature through stable identifiers/versions/configuration signatures/delegate contracts, never RTTI/compiler/layout/function addresses. Operational local mechanisms normally do not.

## 3.11 Locked defaults / memory

MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DeduplicationWindowBits128; MaxSameRouteAttempts3; MaxRoutesAttempted4; protected capacities 8/4/8/8; unreachable full-member retention60s; tombstone retention~5min.

Provisional fixed/cardinality metadata ≈60,160 B plus starting task stacks ≈10,000 B = ≈70,160 B before variable payload/control/reassembly/runtime overhead.

`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`

---

# 4. Current implementation state

## 4.1 Lower/platform libraries

- **System:** canonical exact-16-B DeviceIdentifier validated.
- **Primitive:** family/version/message/correlation/fingerprint vocabulary and central allocation boundaries validated; concrete family IDs unallocated.
- **Radio:** bounded direct-link logical transfer; compact generation-safe `RadioPeerHandle`/registry; `RadioTransport` emits SourcePeer, supports preferred peer-handle unicast, invalidates peers on explicit/interface/Stop lifecycle and exposes Observable peer lifecycle. Peer saturation never falls back to raw provider address as identity.
- **ESP32:** shared-PHY coordinator / Raw80211 coexistence validated.
- **NRF24:** bounded logical transfer/direct-link semantics validated.
- **State:** structural State migration validated, including non-owning local binding, authoritative/effective availability split and non-reentrant notifications.
- **Command:** intrinsic RPC/request-response/timeout/reply model removed; Primitive-backed bounded CommandMessage validated. `CommandResult` wording still needs local-disposition clarification.
- **Event:** provenance/context separation and EVTT v2 validated; distributed EventTypeId remains unresolved.
- **Security audit:** current `structural_realignment` provides generic crypto/protection facilities (`IRandomSource`, key/provider, AEAD, DataProtector, replay and secure transport components) but no existing Mesh identity/admission handshake abstraction. Mesh must compose these facilities behind its own outcome-oriented security authority/admission boundary; exact handshake/control wire schema remains intentionally unspecified.

## 4.2 Mesh validated foundations

- core identity/value types and locked limits.
- `MembershipTombstoneTable<64>`; `DeduplicationWindow<128>`; nonwrapping `MeshMessageIdGenerator`.
- `InboundDeliveryReservationTable<8>`, `AuthenticatedMembershipTable<32>`, `InboundDeliveryCoordinator`.
- `MembershipRetentionCoordinator`, `MembershipLifecycleCoordinator`, injected liveness policy and `MembershipLivenessTracker<32>`.
- `LivenessProbeReservationTable<4>`, injected `IMeshLivenessProbePolicy`, wire-neutral `ILivenessProbeInitiator` and coordinator.
- `PendingNeighbourCandidateTable<8>`, `InboundAuthenticationReservationTable<4>`, `AdmissionPromotionCoordinator`.
- `PrimitiveReceiverRegistry<8>` and deterministic `AdvertisedPrimitiveSupportSnapshot<8>`; unresolved Mesh family ID remains omitted.
- `MeshRadioRegistry<4>`, `NeighbourDiscoveryCoordinator`, `RadioPeerLifecycleCoordinator`.
- injected `IMeshTrafficGovernor` + protected default 8/4/8/8 governor; injected finite `IControlWorkLifetimePolicy`.
- `TopologyLinkIdentity`, `DirectedTopologyLink<TCharacteristics>`, complete authority-scoped `TopologySnapshot<TCharacteristics,96>`.
- injected `ITopologyFreshnessPolicy`/tracker and `ITopologyPublicationPolicy`/coordinator; no universal thresholds invented.
- `TopologyGraphStore<TCharacteristics,96,32>` with graph-wide 96-link bound.
- `ResolvedRoute<16>` local loop-free route artifact; `RouteCache<32,16>` disposable generation-safe cache with exact pair/endpoint/authority invalidation and no hidden eviction.
- `IRoutingStrategy<TCharacteristics>` with bounded `RoutingEvidence` and structural output validation; no universal RouteCost/source-routing contract.
- `IRouteAttemptPolicy` / `IRetryPolicy`, default frozen ceilings 3 same-route attempts / 4 distinct routes.
- `IRouteRevalidationPolicy` and `RoutePlanner`: cached hints are copied, structurally checked and revalidated against current composition evidence before use. Rejected hints are removed and trigger fresh strategy planning. Fresh strategy results must pass the same revalidation policy before use/cache insertion. Cache saturation cannot turn successful planning into failure.
- `RouteAttemptCoordinator`: tiny per-delivery counter state applies route/retry policy while treating one immutable absolute monotonic deadline as an outer hard stop. It distinguishes Complete / RetryCurrentRoute / ReplanDistinctRoute / StopDeadlineExpired / StopPermanentFailure / StopAttemptLimit. It owns no payload, route, Radio work or scheduler.
- umbrella header exports all current foundations.

Full NodeProfile remains blocked on Group identifier semantics. Topology normalized characteristic representation and default freshness/publication thresholds intentionally remain policy/concrete-build decisions rather than hidden constants.

## 4.3 Validation record

- `33785734451`, `33786121103`, `33786446663`, `33786968405`, `33787189204`, `33789240336`: initial Mesh identity/membership/delivery/liveness foundations green.
- `33792457888`, `33792536582`: lifecycle/pre-auth resources green.
- `33793151350`: Primitive receiver registry suite green after correcting include path.
- Radio `33793581249`, `33797082636`, `33798291517`, `33799876449`: peer registry/transport integration/enumeration/Observable lifecycle native + ESP32 green.
- Mesh `33793950787`, `33794119177`, `33797422944`, `33797806648`, `33798395545`: admission promotion, Radio discovery, traffic governor and control lifetime green.
- Mesh `33800204015`, `33800502633`, `33801088318`, `33801088364`: peer-loss cleanup and liveness probing green.
- Mesh `33801574428`, `33801574478`: deterministic advertised primitive support green.
- Mesh `33804484722`, `33804812963`: directed complete topology snapshot plus authority isolation green.
- Mesh `33805171842`, `33805171816`: topology freshness dedicated/general suites green.
- Mesh `33807676687`, `33807676751`: corrected topology evolution/general CI green.
- Mesh `33808650254`: bounded route/cache regression and umbrella green.
- Mesh `33810738804`: corrected routing-policy regression green after a test-only namespace qualification fix.
- Mesh `33815259671`: route planner/cache revalidation + immutable-deadline attempt coordinator regression green along with topology publication, global graph, route cache, routing policy and complete umbrella build.
- **No Actions-budget symptom has occurred.**

## 4.4 Critical boundaries

- Pre-auth state cannot mutate authenticated membership/dedup/liveness.
- Liveness evidence must already be authenticated for the exact incarnation; an outstanding probe is not authentication.
- Radio peer handles are local Radio capabilities, never Mesh identity; peer invalidation never removes authenticated membership.
- Traffic classes remain independently protected; control work has finite lifetime.
- Primitive support snapshot omits unresolved Mesh control family ID.
- Full NodeProfile must not be completed until Group identifier semantics is established.
- Topology truth, publication materiality, remote freshness and routing interpretation remain separate.
- `MaxTopologyLinks=96` is graph-wide, never per member.
- Route plans/cache entries are disposable local hints, never distributed authority or source-route contracts.
- Every cached route is revalidated against current evidence. A cache hit can never bypass membership, freshness, link usability or current routing policy.
- Fresh strategy output is also subjected to structural/current-evidence validation before use.
- Cache pressure never changes the semantics of a successfully planned route.
- Same-route and distinct-route retry ceilings are bounded independently; neither overrides the immutable delivery deadline.
- `RouteAttemptCoordinator` owns counters/decision only and deliberately does not own scheduling, Radio submission or payload lifetime.
- No universal RouteCost, topology freshness threshold or publication-materiality threshold has been invented.
- Security composition must reuse ESPressio-Security primitives without inventing a Mesh handshake schema merely to satisfy an interface.
- No concrete ESPressio `PrimitiveFamilyId`, distributed EventTypeId, Event MessageId strategy or Group identifier may be invented autonomously.

---

# 5. Immediate next work

1. Add the narrow forwarding-attempt execution boundary that maps one currently validated route's next hop to current local Radio peer execution and feeds technology-independent direct-link outcomes back into `RouteAttemptCoordinator`. Keep Radio address/peer mechanics below Mesh routing identity.
2. Ensure `RemainingHopLimit` is decremented only after a successful Mesh forwarding transition, never when planning, queueing, Radio admission or failed transmission occurs.
3. Continue Security composition toward a minimal outcome-oriented `IMeshSecurityAuthority` / `IMeshAdmissionPolicy` boundary only where wire-neutral; reuse Security crypto primitives and do not invent a handshake schema.
4. Reconcile Command `CommandResult` source/docs wording so `success` unambiguously describes local parsing/validation/dispatch/handler disposition, never requested-operation completion; avoid gratuitous public rename.
5. Keep Event distributed identity work paused pending Simon's EventTypeId/message-ID decisions.
6. Continue traffic-governed queues → clock coordination → adapters; create/verify MeshAdapters propagation branch when adapter work begins.

---

# 6. Chronological UTC work log

- **2026-09-03T12:59:05Z** — Simon authorized implementation; created propagation branches and authoritative handoff.
- **2026-09-03T13:13:44Z–17:53:20Z** — System/Primitive/Radio/ESP32/NRF24/State/Command/Event propagation and initial Mesh identity, delivery, dedup, membership and retention foundations implemented/validated; unresolved public ID decisions left untouched.
- **2026-09-03T18:12:00Z–18:47:20Z** — Policy-driven liveness, membership lifecycle and bounded pre-auth resources validated.
- **2026-09-03T18:49:00Z–19:01:40Z** — Primitive receiver registry, Radio peer handles, peer-bound candidates and authenticated promotion validated.
- **2026-09-03T19:31:00Z–19:47:00Z** — RadioTransport peer-handle integration, MeshRadioRegistry/discovery, protected traffic governor, peer enumeration and finite control-work lifetime validated.
- **2026-09-03T20:01:00Z–20:20:00Z** — Observable peer lifecycle, pre-auth peer-loss convergence, bounded policy-driven liveness probing and deterministic advertised primitive support validated.
- **2026-09-03T20:43:00Z–20:46:00Z** — Audited current ESPressio-Security abstractions; generic crypto exists but no Mesh admission handshake. No wire schema invented.
- **2026-09-03T20:46:00Z–20:57:00Z** — Implemented bounded complete directed topology plus policy-driven remote freshness; green.
- **2026-09-03T21:18:00Z–21:23:52Z** — Added injected topology-publication materiality and globally bounded graph storage; corrected dedicated workflow coverage; green.
- **2026-09-03T21:31:00Z–21:34:25Z** — Added bounded `ResolvedRoute<16>` and `RouteCache<32,16>` local planning/cache foundations; run `33808650254` green.
- **2026-09-03T21:56:00Z–21:58:34Z** — Added injected routing strategy and independent same-route/distinct-route retry policies; corrected a test-only namespace error; run `33810738804` green.
- **2026-09-03T22:52:00Z–22:55:20Z** — Added `IRouteRevalidationPolicy` + `RoutePlanner`, exact source/destination cache invalidation, and deadline-aware `RouteAttemptCoordinator`. Regression verifies cache hits cannot bypass current validation, rejected hints are removed before re-planning, newly planned routes use the same validator, 3/4 route ceilings remain enforced, and deadline/permanent/delivered outcomes terminate appropriately. Dedicated Topology Evolution run `33815259671` passed all routing/topology regressions and the umbrella. No public/wire identifier decisions crossed.
