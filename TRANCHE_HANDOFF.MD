# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.** System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal and Event provenance propagation are validated. Mesh has validated bounded membership/delivery/liveness/admission foundations, Radio peer lifecycle, Primitive receiver exposure, traffic governance, topology storage/freshness/publication, route planning/cache/retry orchestration, authenticated direct-peer bindings, authenticated next-hop Radio submission, qualified direct-link evidence, and bounded wire-neutral destination-delivery acknowledgement tracking. Radio peer acknowledgement remains strictly separate from Mesh delivery acknowledgement. Command's repository/Wiki documentation has been realigned from the removed RPC model to asynchronous-intent/local-disposition semantics; the remaining source-level IDE comment on `CommandResult::success` still needs wording correction without executable change.

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

Command is asynchronous intent. It has no intrinsic wait/acceptance/completion/result timeout/destination/reply semantics. `CommandResult` describes local parser/validation/middleware/dispatch/callback disposition only and must never imply requested-operation completion. A local success may merely mean asynchronous application work was accepted/queued. Later progress/completion belongs to independent Event/State messages, optionally correlated through `CorrelationId`.

Event is an occurrence with EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote origin belongs to dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State remains application-owned/non-owningly bound; remote replicas are State-owned. Availability is separate from value. Wire concepts are Publication, Availability, Subscribe, Unsubscribe and Resynchronize; no ReplicaAcknowledgement/generic Disconnect.

## Membership / liveness / tombstones
Admission: Unknown→Discovered→Authenticating→Validating→Joining→Active. DeviceIdentifier is never authentication. Security/admission stays behind injected authority/policy abstractions; Mesh performs no cryptography. Direct-neighbour authentication and end-to-end origin authentication are distinct.

Reachability is Unknown/Reachable/Suspect/Unreachable via injected liveness policy. Approximate defaults: 5 s Suspect / 15 s Unreachable; valid authenticated evidence may immediately restore Reachable. Full unreachable record retention default 60 s. Active probes independently bounded to 4.

Membership tombstones are independently bounded to 64 and retain only identity/incarnation/local deadline/disposition. Default retention ~5 min. Deterministic bounded eviction may lose local history but never weakens authentication/admission.

## Delivery / dedup / destinations
Node, Group, CapabilitySelector and Broadcast are API destinations. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set then independent Node deliveries. Broadcast is bounded best-effort flood, one MessageId, no frozen recipient set and no complete-delivery promise.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Unseen inbound delivery reserves bounded InProgress state; definitive disposition commits dedup, retryable/resource-unavailable releases without commit. Old-incarnation traffic never enters current dedup state.

Mesh delivery ACK means destination-framework delivery confirmation only, never requested-operation completion. It is distinct from Radio peer acknowledgement. Sender-local pending-ACK state is keyed by exact destination `DeviceIdentifier + MembershipIncarnation + MeshMessageId` and uses the delivery's existing immutable absolute deadline. A later incarnation cannot acknowledge an older delivery.

The architecture does **not** currently freeze one universal cardinality for all concurrently pending delivery acknowledgements across application, selective-multicast and control work. Therefore `DeliveryAcknowledgementTracker<Capacity>` requires an explicit finite composition-supplied capacity rather than silently inventing a new global default. No ACK wire schema or control `PrimitiveFamilyId` is implied by the tracker.

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

Provisional baseline ~70,160 B fixed/cardinality + starting stacks before variable payload/control/reassembly/runtime overhead: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`. New direct-peer/fixed metadata and whichever finite delivery-ACK capacity a composition selects must be included in the next measured accounting; the earlier figure is not a final guarantee.

---

# 4. Current implementation state

Validated lower/platform work: System canonical DeviceIdentifier; Primitive conceptual IDs/version/fingerprint/family-registry boundaries; Radio bounded logical transfer, RadioPeerRegistry, peer lifecycle Observable, peer-handle unicast and qualified direct-link evidence aggregation; ESP32 shared Wi-Fi/Raw80211 PHY coordinator with Raw80211 submission-only evidence; NRF24 direct-link semantics with synchronous completed/acknowledged evidence; State structural realignment; Command intrinsic RPC machinery removed; Event provenance/context separation and EVTT v2 with distributed identity paused.

Command documentation reconciliation on its propagation branch now includes: README rewritten around asynchronous intent and local `CommandResult` disposition; stale Wiki RPC pages replaced in-place for link stability (`Asynchronous-Command-Routing`, `Request-and-Response-Envelopes`, `Pending-Request-Management`, `Response-Expectations-and-Timeouts`, `Response-Route-Integration`, `Results-and-Errors`); Wiki Home and sidebar realigned. No package version fields were changed. The remaining IDE source comment in `ESPressio_Command.hpp` still says “completed successfully” and is queued for a comments-only source edit.

Validated Mesh foundations: membership tombstones/dedup/MessageId/inbound reservations; authenticated membership/retention/lifecycle/liveness/probes; pre-auth candidate/auth resources and authenticated promotion; PrimitiveReceiverRegistry<8> and support snapshot; MeshRadioRegistry<4>/discovery/peer cleanup; traffic governor/control lifetime; directed topology/graph-wide store/freshness/publication; ResolvedRoute<16>/RouteCache<32>/IRoutingStrategy/revalidation/planner; IRouteAttemptPolicy/IRetryPolicy/deadline-aware RouteAttemptCoordinator; AuthenticatedDirectPeerBindingTable<96>; DirectPeerLifecycleCoordinator; `CommitSuccessfulForwardingTransition()` as sole hop-budget mutation; ForwardingSubmissionCoordinator; `ForwardingDirectLinkEvidence::{None,SubmissionAccepted,TransmissionCompleted,PeerAcknowledged}` without conflating Radio evidence with Mesh delivery; `DeliveryAcknowledgementTracker<Capacity>` for sender-local exact-incarnation ACK reservations, authenticated ACK application, cancellation and deadline purge. No control family ID or ACK wire representation has been introduced.

---

# 5. Validation record

Earlier green Mesh runs include `33785734451`, `33786121103`, `33786446663`, `33786968405`, `33787189204`, `33789240336`, `33792457888`, `33792536582`, `33793151350`, `33793950787`, `33794119177`, `33797422944`, `33797806648`, `33798395545`, `33800204015`, `33800502633`, `33801088318`, `33801088364`, `33801574428`, `33801574478`, `33804484722`, `33804812963`, `33805171842`, `33805171816`, `33807676687`, `33807676751`, `33808650254`, `33810738804`, `33815259671`. Earlier Radio green runs include `33793581249`, `33797082636`, `33798291517`, `33799876449`.

Recent forwarding validation: Mesh Topology Evolution `33845580307` / CI `33845580185` green for direct-peer transition foundations; Mesh Topology Evolution `33845944637`, CI `33845944459`, Topology Freshness `33845944550`, Control Work Lifetime `33845944552` green for authenticated forwarding submission; Radio `33848107862` passed native + ESP32-worker jobs including direct-link evidence aggregation; NRF24 `33847918464` passed; Mesh Topology Evolution `33848231756` and CI `33848231773` passed evidence-aware forwarding.

Wire-neutral delivery acknowledgement tracking: commits `17427c38db09b45be554b8cc2dad17aa4df5761d` (tracker), `93b020cab46ae4568f2cf6c2c727bf95d863035e` (regression), `26fc968ebb0db5bc4e14a71c5594e3c05cbfcfe7` (umbrella), `162550e99cebe500fa13b8ee6924b6c69555dc3c` (corrected workflow). Topology Evolution **`33849228375` passed**, including the new ACK regression and complete umbrella build; general CI **`33849228424` passed** its full host regression set; Control Work Lifetime `33849228313` passed. The first workflow edit contained a test-only include-path typo and was corrected before its run was used as validation evidence.

Command documentation commits: `5ec46c4220416cd4a248cefd47113421dda774c0` local-result semantics; `166dd609a8a84cc55cf8b91ff98f35cbdc2770c0` async-routing rewrite; `9daf3dd9631b64398aa9b4ce6ad172b581bcbadb` CommandMessage envelope rewrite; `b8fb95c75987cc48d966d8ebd380baa684925e7b` pending-work ownership; `19f0110491ce76ef6ff43289840d7b151f2cf951` timeout separation; `cc67648e473aabbc1e24855eec6baa07bd1e05aa` transport/correlation separation; `2af819aeb555bfc2ec9315f21b33a754977ef018` sidebar; `d852fe1e7c80f7b726b94c90721c17dce7b95954` Wiki Home; `33326d71f967c32e48e4f0269f86df2cba79dfe1` README rewrite. Command host tests in run `33848885697` passed; follow-up Command validation should be rechecked before tranche completion. No Actions-budget symptom.

---

# 6. Critical boundaries

Pre-auth state cannot mutate authenticated membership/dedup/liveness. Radio peer handles are local capabilities only. Direct executable peer binding is incarnation-qualified and created only after authenticated promotion. Forwarding resolves current authenticated membership and exact incarnation before Radio execution. Radio admission, transmission completion and peer acknowledgement are distinct facts. Radio peer acknowledgement is not Mesh delivery acknowledgement. HopLimit is never consumed by Radio evidence alone. Sender-local delivery ACK tracking accepts only the exact authenticated destination incarnation and MessageId and uses the immutable delivery deadline. Its finite capacity is composition-supplied because no universal pending-ACK capacity is frozen. Route/cache is disposable local optimization. Command local result is not requested-operation completion. No universal RouteCost, hidden cache eviction, hidden topology threshold or hidden retry ceiling introduced. No unresolved Primitive/Event/Group public identifier or ACK control schema allocated. Security composition must reuse ESPressio-Security without inventing handshake/control schema.

---

# 7. Immediate next work

1. Add only the next wire-neutral Mesh delivery-ACK coordination boundary: destination-side semantic acceptance may request an ACK, and an already-authenticated ACK may complete sender-local tracking. Do not define/encode the control message or allocate a `PrimitiveFamilyId`; stop if that becomes unavoidable.
2. Finish the comments-only `ESPressio_Command.hpp` IDE documentation correction for `CommandResult::success` and callback disposition wording when a safe source-edit path is available; verify no executable change and validate Command CI.
3. Continue wire-neutral `IMeshSecurityAuthority` / `IMeshAdmissionPolicy` composition using ESPressio-Security without inventing a handshake schema.
4. Keep Event identity and full NodeProfile paused until EventTypeId/MessageId and Group identifier decisions.
5. Continue bounded traffic queues → clock coordination → MeshAdapters propagation branch/implementation.
6. Re-run measured fixed-memory accounting before final whole-device capacity declaration, including direct-peer metadata and the selected ACK-tracker capacity.

---

# 8. Chronological UTC log

- 2026-09-03 12:59–17:53 — propagation branches plus System/Primitive/Radio/ESP32/NRF24/State/Command/Event foundations and initial Mesh membership/delivery work.
- 18:12–20:20 — liveness, lifecycle, pre-auth resources, Primitive receivers, Radio peer/discovery lifecycle, traffic governance/control lifetime, liveness probes and advertised support.
- 20:43–20:57 — Security audit and bounded topology/freshness.
- 21:18–21:34 — topology publication materiality, globally bounded graph, route/cache.
- 21:56–22:55 — routing strategy, retry policies, route revalidation/planner and deadline-aware attempt coordinator; `33815259671` green.
- 2026-09-04 06:39–06:49 — authenticated direct peers, lifecycle convergence, explicit forwarding transition and authenticated Radio submission; `33845944637` / `33845944459` green.
- 07:10–07:20 — qualified Radio admission/transmission/peer-ACK evidence; NRF24 qualified completion/ACK, Raw80211 remains submission-only; RadioTransport fragment aggregation; Mesh link evidence remains distinct from Mesh delivery. Radio `33848107862`, NRF24 `33847918464`, Mesh `33848231756` / `33848231773` green.
- 07:24–07:30 — Command documentation audit exposed RPC-era Wiki/README content despite runtime RPC removal. Replaced stale response-envelope/pending-request/timeout/reply-route docs with asynchronous-intent, local-disposition and correlation semantics; rewrote README with propagation dependency refs and no package-version mutation. Host tests green.
- 07:31–07:34 — added fixed-capacity, wire-neutral `DeliveryAcknowledgementTracker<Capacity>` keyed by exact destination identity/incarnation/MessageId and immutable delivery deadline. Deliberately left capacity composition-supplied rather than inventing a global bound; no control family ID or ACK wire schema introduced. Corrected a workflow-only include typo before validation. Topology Evolution `33849228375` and general CI `33849228424` green.