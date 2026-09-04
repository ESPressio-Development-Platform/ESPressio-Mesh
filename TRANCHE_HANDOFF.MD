# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.** System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal and Event provenance propagation are validated. Mesh now has validated bounded membership/delivery/liveness/admission foundations, Radio peer lifecycle, Primitive receiver exposure, traffic governance, topology storage/freshness/publication, route planning/cache/retry orchestration, authenticated direct-peer bindings, and an explicit successful-forwarding transition boundary.

This is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`. Do not alter public/distributed semantics, dependency direction, authentication/identity semantics, locked bounds or Policy without Simon's explicit decision.

**Still unresolved public/wire decisions:** concrete ESPressio `PrimitiveFamilyId` allocations; distributed EventTypeId assignment; conceptual Event message-ID generation scope; Group identifier contract. Do not invent them.

---

# 1. Branch scope

Primary:
- `ESPressio-Mesh`: `structural_realignment_propagation`

Affected:
- `ESPressio-System`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Primitive`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Radio`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-ESP32`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-NRF24`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-State`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Command`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Event`: `structural_realignment_propagation_ESPressio-Mesh`

`ESPressio-MeshAdapters` remains architecturally required but is not yet branched/implemented. Repository name correction: **`ESPressio-Primitive` is singular**.

---

# 2. Mandatory Policy rules

- Ordinary libraries never call hardware/platform APIs directly; use the relevant ESPressio abstraction.
- Reuse existing ESPressio abstractions; avoid duplicate responsibilities.
- No circular dependencies, including optional ones. Stop and ask Simon if one appears unavoidable.
- Participating manifests/tests/jobs/docs/source references must target matching propagation branches.
- Documentation and IDE doc comments move with APIs; useful public discrete components should receive Arduino IDE + PlatformIO demos where appropriate.
- Internal diagnostics use compile-switchable `ESPressio-Logging` after checking its current API.
- Optional callbacks use `ESPressio-Observable`; Event bridges are optional and live with the source subsystem.
- Do not change version numbering merely because of tranche work and do not preserve legacy behavior unless Simon requests it.
- Maintain this handoff after meaningful implementation/decision/validation changes. At full completion offer it to Simon, then delete it from Git.
- If Actions begins failing almost immediately due monthly task budget exhaustion, explicitly record that and stop treating CI as correctness evidence. No such symptom has occurred.

---

# 3. Frozen architecture

## Layering

System owns foundational identity/types/services. Primitive owns dependency-neutral conceptual-message vocabulary. Command/Event/State own their application semantics. Radio owns opaque direct-link physical/link transport and hop-local packetization/reassembly. Mesh owns membership/topology/routing/control/logical addressing/admission/multi-radio forwarding/end-to-end delivery and must not depend on Command/Event/State. MeshAdapters sits above both sides. Timing owns synchronization math/quality; Mesh coordinates root/parent and Radio owns direct timestamp mechanics.

## Identity

- DeviceIdentifier: exact 16 B, permanent transport-independent identity; zero invalid; never authentication.
- MeshIdentifier: exact nonzero 16 B app-supplied.
- MembershipIncarnation: exact nonzero 16 B participation instance.
- MeshNodeAlias uint16, RadioIdentifier uint8 (0 invalid, 1–254 usable, 255 reserved).
- MeshMessageId uint64 nonzero monotonic/nonwrapping scoped to authenticated source DeviceIdentifier+MembershipIncarnation.
- ProfileGeneration/TopologyGeneration uint64 nonzero.
- CanonicalName mandatory Mesh-unique printable ASCII 1–32 B, exact case-sensitive comparison.
- ContractFingerprint exact 32 B SHA-256 semantic digest, not authentication.

## Primitive / Command / Event / State

Primitive family range is uint16: 0 invalid, 0x0001–0x7FFF ESPressio-defined, 0x8000–0xFFFE private, 0xFFFF reserved. Concrete ESPressio IDs remain unallocated.

Command is asynchronous intent, not RPC: no intrinsic wait/acceptance/completion/result timeout/destination/reply semantics. Existing `CommandResult` must be described strictly as local parser/validation/dispatch/handler disposition, never requested-operation completion.

Event is an occurrence with EventTypeId, SchemaVersion, conceptual MessageId, priority, payload, optional CorrelationId. Local/Remote origin belongs to dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and MessageId generation remain unresolved.

StateAddress = DeviceIdentifier + StateTypeId. Local state remains application-owned/non-owningly bound; remote replicas State-owned. Availability is separate from value. Wire concepts: Publication, Availability, Subscribe, Unsubscribe, Resynchronize. No ReplicaAcknowledgement/generic Disconnect.

## Membership/security/liveness

Admission progression: Unknown→Discovered→Authenticating→Validating→Joining→Active. DeviceIdentifier is never authentication. Security/admission remains behind injected authorities/policies; Mesh does no crypto. Direct-neighbour authentication and end-to-end origin authentication are distinct. Reachability is Unknown/Reachable/Suspect/Unreachable with injected policy; valid authenticated evidence may immediately restore Reachable. Default approximate liveness thresholds are 5s Suspect / 15s Unreachable; full unreachable record retention default 60s.

Tombstones are independently bounded to 64, compact and local, with dispositions LocallyForgotten/SupersededIncarnation/AuthoritativeLeave; default retention ~5min. Tombstone pressure never weakens authentication/admission.

## Routing/topology

Topology is a globally bounded directed graph, each member authoritative only for its own outbound set. A→B and B→A are distinct. `TopologyGeneration` versions one complete outbound set per authority+incarnation. New complete generation replaces older set; same-generation authenticated retransmission may refresh local freshness but not semantics. `MaxTopologyLinks=96` is graph-wide, never per member.

Topology stores technology-independent observations and **does not define a universal RouteCost**. Publication materiality uses injected `ITopologyPublicationPolicy`. Remote freshness is local metadata Fresh/Degraded/Stale/Expired through injected `ITopologyFreshnessPolicy`; expired topology is unusable for originating routes.

Routing uses injected `IRoutingStrategy`; route plans are local disposable artifacts, not source-routing wire contracts. Route cache bound 32, maximum route hops 16, default HopLimit 16, same-route attempts 3, distinct routes 4. Cache hits and newly planned routes are revalidated against current membership/topology/link/policy evidence. Immutable absolute delivery deadline bounds every attempt/replan.

`RemainingHopLimit` decrements **only when a successful Mesh forwarding transition is explicitly committed**. It does not decrement during planning, cache lookup, queue admission, `RadioTransport::Send` acceptance, or failed direct-link work.

## Radio/direct peer execution

Radio accepts complete immutable bounded logical transfers to one directly reachable Radio peer and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is process-local generation-safe link capability only, never Mesh identity/distributed topology identity.

Successful authentication may now return an `AuthenticatedDirectPeerBinding` containing authenticated neighbour DeviceIdentifier + MembershipIncarnation plus the local RadioIdentifier and RadioPeerHandle which produced that candidate. `AuthenticatedDirectPeerBindingTable<96>` retains these executable local bindings. Rebinding the same neighbour/local-radio replaces the prior incarnation/peer; exact incarnation is required for resolution. Radio peer invalidation removes executable binding only and **never removes authenticated membership**.

`RadioTransportSendStatus::Accepted` is admission, not proof of successful Mesh forwarding. Hop-limit commitment therefore remains separated from Radio submission.

## Traffic/execution/payload

Protected default traffic capacities are InfrastructureResponse 8, ClockControl 4, GeneralControl 8, Application 8 through injected `IMeshTrafficGovernor`. Control work has finite injected lifetime. Mesh remains decomposed into narrow services; one serialized execution domain is acceptable and no direct FreeRTOS belongs in Mesh. All queues/resources are finite/backpressured. Payload forms remain Borrowed Stable / Bounded Owned / Repeatable Serialized Source with immutable logical bytes for one MeshMessageId across retries/routes/fragments.

## Locked capacities

MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; protected traffic 8/4/8/8.

Provisional memory baseline remains ~70,160 B fixed/cardinality+starting stacks before variable payload/control/reassembly/runtime overhead:
`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`.

---

# 4. Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family registry boundaries.
- Radio bounded logical transfer, RadioPeerRegistry, peer lifecycle Observable, peer-handle unicast.
- ESP32 shared Wi-Fi/Raw80211 PHY coordinator; NRF24 direct-link semantics.
- State structural realignment.
- Command intrinsic RPC machinery removed; wording cleanup remains.
- Event provenance/context separation and EVTT v2; distributed identity paused.

Validated Mesh foundations:
- membership tombstones, dedup, message-ID generator, inbound reservations/coordinator.
- authenticated membership, retention, lifecycle, liveness and active probes.
- bounded pre-auth candidate/authentication resources and authenticated admission promotion.
- PrimitiveReceiverRegistry<8> and advertised support snapshot.
- MeshRadioRegistry<4>, neighbour discovery, Radio peer lifecycle cleanup.
- traffic governor + control-work lifetime.
- directed complete topology snapshot, graph-wide TopologyGraphStore<96,32>, freshness and publication policies.
- ResolvedRoute<16>, RouteCache<32,16>, IRoutingStrategy, IRouteRevalidationPolicy/RoutePlanner.
- IRouteAttemptPolicy/IRetryPolicy and deadline-aware RouteAttemptCoordinator.
- `AuthenticatedDirectPeerBindingTable<96>`.
- admission promotion can emit only the **authenticated** peer binding; untrusted claim never becomes authority.
- `DirectPeerLifecycleCoordinator` removes stale executable peer bindings on Radio invalidation without mutating membership.
- `CommitSuccessfulForwardingTransition()` is the sole new hop-budget mutation primitive; it refuses decrement at zero.

---

# 5. Validation record

Earlier green Mesh/Radio runs include `33785734451`, `33786121103`, `33786446663`, `33786968405`, `33787189204`, `33789240336`, `33792457888`, `33792536582`, `33793151350`, Radio `33793581249`, `33797082636`, `33798291517`, `33799876449`, Mesh `33793950787`, `33794119177`, `33797422944`, `33797806648`, `33798395545`, `33800204015`, `33800502633`, `33801088318`, `33801088364`, `33801574428`, `33801574478`, `33804484722`, `33804812963`, `33805171842`, `33805171816`, `33807676687`, `33807676751`, `33808650254`, `33810738804`, `33815259671`.

Current continuation:
- commit `a22bb18d03def92eaf21202e7e82bcccf1501057` added dedicated direct-peer/forward-transition workflow coverage.
- Topology Evolution run **`33845580307` passed**, including topology publication, global graph, route/cache, routing policy, route planner/attempts, direct peer binding/hop transition regression and umbrella build.
- General CI run `33845580185` passed its full host regression suite against the same commit.
- subsequent admission regression was strengthened to verify emitted peer binding uses separately authenticated identity and remains empty on conflicting-incarnation failure.
- no Actions-budget symptom has occurred.

---

# 6. Critical boundaries

- Pre-auth state cannot mutate authenticated membership/dedup/liveness.
- Radio peer handles are local capabilities only; their loss never implies member departure.
- Direct executable peer binding is incarnation-qualified and may only be created after authenticated promotion.
- `RadioTransport::Send` acceptance is not a successful Mesh hop transition and cannot consume HopLimit.
- Route/cache state is disposable local optimization and never distributed authority.
- No universal RouteCost or hidden cache-eviction/topology threshold has been introduced.
- No unresolved Primitive/Event/Group public identifier has been allocated.
- Security composition must reuse ESPressio-Security primitives without inventing a handshake/control wire schema.

---

# 7. Immediate next work

1. Add a narrow forwarding submission/coordinator layer which resolves a validated route's local next-hop against current authenticated membership + `AuthenticatedDirectPeerBindingTable`, submits through Radio peer handle, and treats submission acceptance separately from later direct-link/Mesh transition evidence.
2. Add explicit outcome translation only where Radio can provide a technology-independent completed direct-link result; do not label mere admission as delivered.
3. Continue wire-neutral `IMeshSecurityAuthority` / `IMeshAdmissionPolicy` composition using ESPressio-Security facilities without inventing handshake schema.
4. Reconcile Command `CommandResult` source/docs wording to local-disposition semantics.
5. Keep Event identity and full NodeProfile paused until Simon resolves Event IDs/MessageId and Group identifier semantics.
6. Continue bounded traffic queues → clock coordination → MeshAdapters propagation branch/implementation.

---

# 8. Chronological UTC log

- 2026-09-03 12:59–17:53 — propagation branches plus System/Primitive/Radio/ESP32/NRF24/State/Command/Event foundations and initial Mesh membership/delivery work.
- 18:12–20:20 — liveness, lifecycle, pre-auth resources, Primitive receivers, Radio peer/discovery lifecycle, traffic governor/control lifetime, liveness probes and advertised support.
- 20:43–20:57 — Security audit and bounded topology/freshness.
- 21:18–21:34 — publication materiality, globally bounded topology graph, bounded route/cache.
- 21:56–22:55 — routing strategy, retry policies, route revalidation/planner and immutable-deadline attempt coordinator; dedicated run `33815259671` green.
- 2026-09-04 06:39–06:44 — added authenticated direct-peer binding retention, admission binding capture, explicit successful forwarding transition/hop-limit primitive and dedicated regression. Topology Evolution `33845580307` and general CI `33845580185` green.
- 06:44 onward — strengthened admission regression and added direct peer lifecycle convergence; executable binding loss remains link-local only and cannot remove membership.
