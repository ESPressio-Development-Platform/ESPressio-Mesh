# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.** System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal and Event provenance propagation are validated. Mesh has validated bounded membership/delivery/liveness/admission foundations, Radio peer lifecycle, Primitive receiver exposure, traffic governance, topology storage/freshness/publication, route planning/cache/retry orchestration, authenticated direct-peer bindings, authenticated next-hop Radio submission, and qualified direct-link evidence that remains strictly separate from Mesh delivery acknowledgement.

This is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`. Do not alter public/distributed semantics, dependency direction, authentication/identity semantics, locked bounds or Policy without Simon's explicit decision.

**Unresolved public/wire decisions:** concrete ESPressio `PrimitiveFamilyId` allocations; distributed EventTypeId assignment; conceptual Event message-ID generation scope; Group identifier contract. Do not invent them.

---

# 1. Branch scope

Primary: `ESPressio-Mesh` → `structural_realignment_propagation`.

Affected propagation branches: `ESPressio-System`, `ESPressio-Primitive`, `ESPressio-Radio`, `ESPressio-ESP32`, `ESPressio-NRF24`, `ESPressio-State`, `ESPressio-Command`, `ESPressio-Event` → `structural_realignment_propagation_ESPressio-Mesh`.

`ESPressio-MeshAdapters` remains architecturally required but is not yet branched/implemented. Repository-name correction from Simon: **`ESPressio-Primitive` is singular**.

---

# 2. Mandatory Policy rules

- Ordinary libraries never call hardware/platform APIs directly; use the appropriate ESPressio abstraction.
- Reuse existing ESPressio abstractions and avoid duplicate responsibilities.
- No circular dependencies, including optional ones. Stop and ask Simon if one appears unavoidable.
- Participating manifests/tests/jobs/docs/source references target matching propagation branches.
- Documentation and IDE doc comments move with APIs; useful public discrete components receive Arduino IDE + PlatformIO demos where appropriate.
- Internal diagnostics use compile-switchable `ESPressio-Logging` only after checking its current API.
- Optional callbacks use `ESPressio-Observable`; Event bridges remain optional and live with the source subsystem.
- Do not change version numbering merely because of tranche work. Do not preserve legacy behavior unless Simon requests it.
- Maintain this handoff after meaningful implementation/decision/validation changes. At full completion offer it to Simon, then delete it from Git.
- If Actions begins failing almost immediately because monthly task budget is exhausted, record that explicitly and stop treating CI as correctness evidence until reset. **No such symptom has occurred.**

---

# 3. Frozen architecture

## Layering
System owns foundational identity/types/services and canonical `DeviceIdentifier`. Primitive owns dependency-neutral conceptual-message vocabulary. Command owns asynchronous intent, not RPC. Event owns occurrence semantics. State owns authoritative fact/replication semantics. Radio owns opaque direct-link transport and hop-local packetization/reassembly/retry/evidence. Mesh owns membership/topology/routing/control/logical addressing/admission/multi-radio forwarding/end-to-end delivery and must not depend on Command/Event/State. MeshAdapters sits above both sides. Timing owns synchronization math/quality; Mesh coordinates root/parent; Radio owns direct timestamp mechanics.

## Identity and fixed types
`DeviceIdentifier`: exact 16 B, permanent transport-independent, zero invalid, never authentication. `MeshIdentifier`: exact nonzero 16 B app-supplied. `MembershipIncarnation`: exact nonzero 16 B participation instance. `MeshNodeAlias`: uint16 Mesh-local routing handle. `RadioIdentifier`: uint8, 0 invalid, 1–254 usable, 255 reserved, incarnation-local. `MeshMessageId`: uint64 nonzero monotonic/nonwrapping scoped to authenticated `(DeviceIdentifier, MembershipIncarnation)`. `ProfileGeneration` / `TopologyGeneration`: uint64 nonzero. `CanonicalName`: mandatory Mesh-unique printable ASCII 1–32 B exact case-sensitive. `ContractFingerprint`: exact 32 B SHA-256 semantic compatibility digest, never authentication.

## Primitive / Command / Event / State
`PrimitiveFamilyId` is uint16: 0 invalid; 0x0001–0x7FFF ESPressio-defined; 0x8000–0xFFFE app/private; 0xFFFF reserved. Concrete ESPressio family IDs remain unallocated.

Command is asynchronous intent. It has no intrinsic wait/acceptance/completion/result timeout/destination/reply semantics. Existing `CommandResult` describes local parser/validation/dispatch/handler disposition only and must never imply requested-operation completion.

Event is an occurrence with EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote origin belongs to dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State remains application-owned/non-owningly bound; remote replicas are State-owned. Availability is separate from value. Wire concepts are Publication, Availability, Subscribe, Unsubscribe and Resynchronize; no ReplicaAcknowledgement/generic Disconnect.

## Membership / liveness / tombstones
Admission: Unknown→Discovered→Authenticating→Validating→Joining→Active. DeviceIdentifier is never authentication. Security/admission stays behind injected authority/policy abstractions; Mesh performs no cryptography. Direct-neighbour authentication and end-to-end origin authentication are distinct.

Reachability is Unknown/Reachable/Suspect/Unreachable via injected liveness policy. Approximate defaults: 5 s Suspect / 15 s Unreachable; valid authenticated evidence may immediately restore Reachable. Full unreachable record retention default 60 s. Active probes independently bounded to 4.

Membership tombstones are independently bounded to 64 and retain only identity/incarnation/local deadline/disposition. Default retention ~5 min. Deterministic bounded eviction may lose local history but never weakens authentication/admission.

## Delivery / dedup / destinations
Node, Group, CapabilitySelector and Broadcast are API destinations. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set then independent Node deliveries. Broadcast is bounded best-effort flood, one MessageId, no frozen recipient set and no complete-delivery promise.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Unseen inbound delivery reserves bounded InProgress state; definitive disposition commits dedup, retryable/resource-unavailable releases without commit. Old-incarnation traffic never enters current dedup state.

Mesh delivery ACK means delivery confirmation only, never requested-operation completion. It is distinct from Radio peer acknowledgement.

## Topology / routing
Topology is a globally bounded directed graph; each member is authoritative only for its own outbound set. A→B and B→A are independent. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide.

Topology stores technology-independent observations and defines no universal `RouteCost`. Publication materiality is injected through `ITopologyPublicationPolicy`. Remote freshness is separate local metadata Fresh/Degraded/Stale/Expired through `ITopologyFreshnessPolicy`; expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`; `ResolvedRoute<16>` is a local disposable artifact, never a source-routing wire contract. `RouteCache<32>` is optimization only. Every cache hit and fresh plan is revalidated against current membership/topology/freshness/link/policy evidence. Same-route attempts bounded to 3, distinct routes to 4, with one immutable absolute delivery deadline across attempts/replans.

`RemainingHopLimit` decrements **only when a successful Mesh forwarding transition is explicitly committed**. It does not decrement during planning, cache lookup, queue admission, Radio submission, Radio transmission completion, Radio peer acknowledgement or failed work. A successful Mesh transition requires Mesh-level evidence that the next Mesh node accepted the forwarded message.

## Radio / executable direct peers
Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is process-local generation-safe link capability only, never Mesh identity or distributed topology identity.

Successful authentication may emit an `AuthenticatedDirectPeerBinding` containing authenticated neighbour DeviceIdentifier + MembershipIncarnation plus local `RadioIdentifier + RadioPeerHandle`. `AuthenticatedDirectPeerBindingTable<96>` retains executable local bindings. Exact current incarnation is required. Radio peer invalidation removes executable binding only and never authenticated membership.

`ForwardingSubmissionCoordinator` resolves a validated route's next hop against current authenticated membership and exact-incarnation direct-peer binding before `RadioTransport::Send(peer, ...)`. It never falls back to raw RadioAddress or pre-auth state.

### Qualified direct-link evidence
Radio distinguishes: (1) submission/admission accepted, (2) transmission completed, (3) peer acknowledged. `RadioSendResult` carries `RadioDirectLinkEvidence` with `RadioTransmissionCompletion::{Unknown,Completed}` and `RadioPeerAcknowledgement::{Unavailable,Unknown,Acknowledged}`. `RadioTransport` aggregates this across every fragment: a logical transfer is Completed only if every fragment is known completed, and Acknowledged only if every fragment is acknowledged.

Concrete validated semantics: ESP32 Raw80211 `esp_wifi_80211_tx()` success remains submission-only (completion unknown, ack unavailable). NRF24 `RF24::write()` successful unicast yields completed + peer acknowledged; successful broadcast yields completed + acknowledgement unavailable.

Even `PeerAcknowledged` is direct-link evidence only. It is **not** a Mesh delivery ACK and cannot consume HopLimit or mean the next Mesh node accepted the Mesh message.

## Traffic / execution / payload
Protected default traffic capacities: InfrastructureResponse=8, ClockControl=4, GeneralControl=8, Application=8 through injected `IMeshTrafficGovernor`. Control work has finite injected lifetime. Mesh remains decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS in Mesh. Every queue/resource is finite/backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

## Locked capacities / provisional memory
MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; protected traffic 8/4/8/8.

Provisional baseline ~70,160 B fixed/cardinality + starting stacks before variable payload/control/reassembly/runtime overhead: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`. New direct-peer/fixed metadata must be included in the next measured accounting; the earlier figure is not a final guarantee.

---

# 4. Current implementation state

Validated lower/platform work: System canonical DeviceIdentifier; Primitive conceptual IDs/version/fingerprint/family-registry boundaries; Radio bounded logical transfer, RadioPeerRegistry, peer lifecycle Observable, peer-handle unicast and qualified direct-link evidence aggregation; ESP32 shared Wi-Fi/Raw80211 PHY coordinator with Raw80211 submission-only evidence; NRF24 direct-link semantics with synchronous completed/acknowledged evidence; State structural realignment; Command intrinsic RPC machinery removed with wording cleanup remaining; Event provenance/context separation and EVTT v2 with distributed identity paused.

Validated Mesh foundations: membership tombstones/dedup/MessageId/inbound reservations; authenticated membership/retention/lifecycle/liveness/probes; pre-auth candidate/auth resources and authenticated promotion; PrimitiveReceiverRegistry<8> and support snapshot; MeshRadioRegistry<4>/discovery/peer cleanup; traffic governor/control lifetime; directed topology/graph-wide store/freshness/publication; ResolvedRoute<16>/RouteCache<32>/IRoutingStrategy/revalidation/planner; IRouteAttemptPolicy/IRetryPolicy/deadline-aware RouteAttemptCoordinator; AuthenticatedDirectPeerBindingTable<96>; DirectPeerLifecycleCoordinator; `CommitSuccessfulForwardingTransition()` as sole hop-budget mutation; ForwardingSubmissionCoordinator; `ForwardingDirectLinkEvidence::{None,SubmissionAccepted,TransmissionCompleted,PeerAcknowledged}` without conflating Radio evidence with Mesh delivery.

---

# 5. Validation record

Earlier green Mesh runs include `33785734451`, `33786121103`, `33786446663`, `33786968405`, `33787189204`, `33789240336`, `33792457888`, `33792536582`, `33793151350`, `33793950787`, `33794119177`, `33797422944`, `33797806648`, `33798395545`, `33800204015`, `33800502633`, `33801088318`, `33801088364`, `33801574428`, `33801574478`, `33804484722`, `33804812963`, `33805171842`, `33805171816`, `33807676687`, `33807676751`, `33808650254`, `33810738804`, `33815259671`. Earlier Radio green runs include `33793581249`, `33797082636`, `33798291517`, `33799876449`.

Recent: Mesh Topology Evolution `33845580307` and CI `33845580185` green for direct-peer/forward-transition foundations. Mesh Topology Evolution `33845944637`, CI `33845944459`, Topology Freshness `33845944550`, Control Work Lifetime `33845944552` green for authenticated forwarding submission.

**Radio `33848107862` passed native and ESP32-worker jobs**, including transport, direct-link evidence aggregation, peer registry, clock synchronization and propagation Event-bridge/ESP32 smoke compilation. **NRF24 `33847918464` passed** against updated Radio. **Mesh Topology Evolution `33848231756` passed**, including forwarding evidence regression and umbrella. **Mesh general CI `33848231773` passed** full host regressions. Mesh Control Work Lifetime `33848231674` passed. No Actions-budget symptom.

Relevant recent commits: Radio `970754aba4551bcfdaaaff3d65e8d11a121fd8f2` (evidence types), NRF24 `6fe85fe0d244d211c66f334f14fa9eab7a6076a6` (qualified concrete evidence), Radio `81627216b7d7b996e08285b84411694376ffa993` (fragment aggregation), Radio `94eb66fc6981b942448e99f50d5aacfab328643c` (evidence regression), Radio `dbf69e953e4335d2b6f507eda36e8db7820e4088` (workflow), Mesh `81b0b87d27ee5a1c9f6fb8bb4fd7805eb1550dbb` (forwarding evidence exposure), Mesh `3bd7da1891f527ade416bdad19fabf4a9cad2bcb` (forwarding evidence regression).

---

# 6. Critical boundaries

Pre-auth state cannot mutate authenticated membership/dedup/liveness. Radio peer handles are local capabilities only. Direct executable peer binding is incarnation-qualified and created only after authenticated promotion. Forwarding resolves current authenticated membership and exact incarnation before Radio execution. Radio admission, transmission completion and peer acknowledgement are distinct facts. Radio peer acknowledgement is not Mesh delivery acknowledgement. HopLimit is never consumed by Radio evidence alone. Route/cache is disposable local optimization. No universal RouteCost, hidden cache eviction, hidden topology threshold or hidden retry ceiling introduced. No unresolved Primitive/Event/Group public identifier allocated. Security composition must reuse ESPressio-Security without inventing handshake/control schema.

---

# 7. Immediate next work

1. Reconcile Command `CommandResult` source/docs wording to local parser/validation/dispatch/handler disposition, never requested-operation completion; avoid gratuitous public rename.
2. Build only wire-neutral Mesh delivery-ACK tracking foundations that can exist without assigning the unresolved Mesh control `PrimitiveFamilyId`; stop and ask Simon if concrete control-wire schema/ID becomes unavoidable.
3. Continue wire-neutral `IMeshSecurityAuthority` / `IMeshAdmissionPolicy` composition using ESPressio-Security without inventing handshake schema.
4. Keep Event identity and full NodeProfile paused until EventTypeId/MessageId and Group identifier decisions.
5. Continue bounded traffic queues → clock coordination → MeshAdapters propagation branch/implementation.
6. Re-run measured fixed-memory accounting before final whole-device capacity declaration.

---

# 8. Chronological UTC log

- 2026-09-03 12:59–17:53 — propagation branches plus System/Primitive/Radio/ESP32/NRF24/State/Command/Event foundations and initial Mesh membership/delivery work.
- 18:12–20:20 — liveness, lifecycle, pre-auth resources, Primitive receivers, Radio peer/discovery lifecycle, traffic governance/control lifetime, liveness probes and advertised support.
- 20:43–20:57 — Security audit and bounded topology/freshness.
- 21:18–21:34 — topology publication materiality, globally bounded graph, route/cache.
- 21:56–22:55 — routing strategy, retry policies, route revalidation/planner and deadline-aware attempt coordinator; `33815259671` green.
- 2026-09-04 06:39–06:49 — authenticated direct peers, lifecycle convergence, explicit forwarding transition and authenticated Radio submission; `33845944637` / `33845944459` green.
- 07:10–07:20 — audited concrete Radio send semantics; added qualified admission/transmission/peer-ACK evidence; NRF24 reports synchronous completed+ACKed unicast and completed/no-ACK broadcast while Raw80211 remains submission-only; RadioTransport aggregates evidence across fragments; Mesh exposes strongest link evidence without treating it as Mesh delivery. Radio `33848107862`, NRF24 `33847918464`, Mesh `33848231756` and `33848231773` green.
