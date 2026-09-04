# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

Validated work now spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership/delivery/routing/security foundations. Radio explicitly distinguishes synchronous send-attempt observation from qualified direct-link completion/acknowledgement evidence. Mesh separately distinguishes Radio evidence, authenticated next-hop Mesh acceptance, end-to-end destination delivery acknowledgement, and requested-operation completion. A bounded, wire-neutral `OutboundDeliveryLifecycle<AcknowledgementCapacity>` now composes these already-frozen delivery stages without introducing a wire schema or new global capacity.

This file is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`. Do not alter public/distributed semantics, dependency direction, authentication/identity semantics, locked capacities or Policy without Simon Stuart's explicit decision.

**Still unresolved public/wire decisions:** concrete ESPressio `PrimitiveFamilyId` allocations; distributed `EventTypeId` allocation/derivation; conceptual Event MessageId generation scope/strategy; Group identifier contract. Do not invent any of these.

---

# 1. Branch scope

Primary:
- `ESPressio-Mesh` → `structural_realignment_propagation`

Affected propagation branches:
- `ESPressio-System`
- `ESPressio-Primitive`
- `ESPressio-Radio`
- `ESPressio-ESP32`
- `ESPressio-NRF24`
- `ESPressio-State`
- `ESPressio-Command`
- `ESPressio-Event`

All affected repositories use `structural_realignment_propagation_ESPressio-Mesh`.

`ESPressio-MeshAdapters` remains architecturally required but is not yet branched/implemented in this tranche. Repository-name correction from Simon: **`ESPressio-Primitive` is singular**.

---

# 2. Mandatory Policy/workflow rules

- Ordinary libraries never call hardware/platform APIs directly; use the appropriate ESPressio abstraction.
- Reuse existing abstractions; avoid duplicate responsibilities.
- No circular dependencies, including optional ones. Stop and ask Simon if one becomes unavoidable.
- Participating manifests/tests/jobs/docs/source references must target matching propagation branches.
- Documentation and IDE-compatible doc comments move with APIs.
- Internal diagnostics use compile-switchable `ESPressio-Logging` only after checking its current API.
- Optional callback surfaces use `ESPressio-Observable`; Event bridges remain optional and live with their source subsystem.
- Do not change version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.
- Maintain this handoff after meaningful implementation/validation/decision changes. At full completion offer it to Simon for download, then delete it from Git.
- If Actions begins failing almost immediately due monthly task/job budget exhaustion, record that explicitly and stop treating CI as correctness evidence until reset. **No budget-exhaustion symptom has occurred.**

---

# 3. Frozen architecture

## 3.1 Layering

- `ESPressio-System`: foundational platform-neutral identities/types/services; canonical `DeviceIdentifier`.
- `ESPressio-Primitive`: dependency-neutral conceptual-message vocabulary only.
- `ESPressio-Command`: asynchronous intent, not RPC.
- `ESPressio-Event`: occurrence semantics.
- `ESPressio-State`: authoritative fact/replication semantics.
- `ESPressio-Radio`: opaque direct-link transport, bounded hop-local packetization/reassembly/retry/evidence.
- `ESPressio-Mesh`: membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end delivery. Mesh must not depend on Command/Event/State.
- `ESPressio-MeshAdapters`: optional integration above Mesh and Command/Event/State.
- `ESPressio-Timing`: synchronization mathematics/discipline/quality; Mesh coordinates distributed root/parent; Radio owns direct timestamp mechanics.

## 3.2 Identity

- `DeviceIdentifier`: exact 16 B, permanent transport-independent, zero invalid, never authentication.
- `MeshIdentifier`: exact nonzero 16 B app-supplied Mesh ID.
- `MembershipIncarnation`: exact nonzero 16 B participation-instance ID.
- `MeshNodeAlias`: uint16 Mesh-local routing handle.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; incarnation-local.
- `MeshMessageId`: uint64 nonzero monotonic/nonwrapping scoped to authenticated `(DeviceIdentifier, MembershipIncarnation)`.
- `ProfileGeneration` / `TopologyGeneration`: uint64 nonzero.
- `CanonicalName`: mandatory Mesh-unique printable ASCII 1–32 B, exact case-sensitive comparison.
- `ContractFingerprint`: exact 32 B SHA-256 semantic compatibility digest, never authentication.

## 3.3 Primitive / Command / Event / State

`PrimitiveFamilyId` is uint16: 0 invalid; `0x0001–0x7FFF` ESPressio-defined; `0x8000–0xFFFE` application/private; `0xFFFF` reserved. Concrete ESPressio family IDs remain unallocated.

Command is asynchronous intent. It has no intrinsic wait/acceptance/completion/result timeout/destination/reply semantics. `CommandResult` means local parser/validation/middleware/dispatch/callback disposition only, never requested-operation completion. Later progress/completion belongs to independent Event/State messages, optionally correlated through `CorrelationId`.

Event is an occurrence with EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote origin belongs to dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and Event MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State stays application-owned/non-owningly bound; remote replicas are State-owned. Availability is separate from value. Wire concepts: Publication, Availability, Subscribe, Unsubscribe, Resynchronize; no ReplicaAcknowledgement/generic Disconnect.

## 3.4 Membership / liveness / tombstones

Admission progression: Unknown → Discovered → Authenticating → Validating → Joining → Active. DeviceIdentifier is never authentication. Direct-neighbour authentication and end-to-end origin authentication are distinct.

Reachability is Unknown / Reachable / Suspect / Unreachable via injected policy. Approximate defaults: 5 s Suspect, 15 s Unreachable. Valid authenticated evidence may immediately restore Reachable. Full unreachable-record retention default 60 s. Active probes independently bounded to 4.

Membership tombstones are independently bounded to 64 and retain only identity/incarnation/local deadline/disposition. Default retention ~5 min. Deterministic bounded eviction may lose local history but never weakens authentication/admission.

## 3.5 Security / admission

`IMeshSecurityAuthority` is Mesh-owned dependency inversion only; Mesh does not depend on `ESPressio-Security`. It receives wire-neutral candidate context containing generation-safe candidate handle, local RadioIdentifier/RadioPeerHandle and explicitly untrusted identity claim. It may return only authentication disposition plus exact `AuthenticatedMeshIdentity` on success.

Authentication dispositions: Authenticated, Pending, Retryable, Rejected, ResourceUnavailable, Invalid. Pending means bounded security work remains active and confers no identity authority.

`IMeshAdmissionPolicy` is independently injected and operates only on already-authenticated identity/context. It performs no cryptography and cannot replace authentication. No Mesh security handshake/control-wire schema has been invented.

## 3.6 Delivery / dedup / acknowledgements

API destinations: Node, Group, CapabilitySelector, Broadcast. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set, then become independent Node deliveries. Broadcast is bounded best-effort flood, one MessageId, no frozen recipient set and no complete-delivery promise.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Unseen inbound delivery reserves bounded InProgress state. Definitive disposition commits dedup; retryable/resource-unavailable releases without commit. Old-incarnation traffic never mutates current dedup state.

Mesh delivery ACK means destination-framework delivery confirmation only. It is not requested-operation completion and is distinct from Radio peer acknowledgement and intermediate next-hop acceptance. Sender-local pending-ACK state is keyed by exact destination DeviceIdentifier + MembershipIncarnation + MeshMessageId and uses the immutable delivery deadline. A later incarnation cannot acknowledge an older delivery.

`DeliveryAcknowledgementTracker<Capacity>` requires an explicit finite composition-supplied capacity because architecture does not freeze one universal ACK cardinality. `DeliveryAcknowledgementCoordinator<Capacity>` creates only wire-neutral `DeliveryAcknowledgementIntent` after definitive destination semantic acceptance and applies only already-authenticated ACKs. It also exposes narrow sender-side `ReservePending` / `ReleasePending` operations so higher lifecycle composition need not depend on tracker internals. No ACK wire schema, route, security mechanism or control family ID is implied.

`OutboundDeliveryLifecycle<AcknowledgementCapacity>` is one per already-bounded Node delivery. It composes route-attempt counters, one `ForwardingTransitionCoordinator`, and optional final-destination ACK tracking while sharing the same immutable `MeshMessageId` and absolute deadline. It owns no payload, route, queue, task, timer, Radio object or wire encoding and does not invent a global ACK capacity. Intermediate forwarders may run with destination-ACK tracking disabled; the originating sender may reserve final-destination ACK capacity before outbound work begins.

## 3.7 Topology / routing / forwarding transition

Topology is a globally bounded directed graph; each member is authoritative only for its own outbound set. A→B and B→A are independent. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide.

Topology stores technology-independent observations and defines no universal `RouteCost`. Publication materiality uses injected `ITopologyPublicationPolicy`. Remote freshness is independent local metadata Fresh / Degraded / Stale / Expired through `ITopologyFreshnessPolicy`; Expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`. `ResolvedRoute<16>` is a local disposable planning artifact, never source-routing wire state. `RouteCache<32>` is optimization only. Every cache hit/fresh plan is revalidated against current membership/topology/freshness/link/policy evidence. Same-route attempts bounded to 3, distinct routes to 4, under one immutable delivery deadline.

`RemainingHopLimit` decrements **only when a successful Mesh forwarding transition is explicitly committed**. It does not decrement during planning, cache lookup, queue admission, Radio submission, Radio transmission completion, Radio peer acknowledgement or failed work. A successful Mesh transition requires authenticated Mesh-level evidence that the exact next Mesh node/incarnation accepted the same MeshMessageId.

`ForwardingTransitionCoordinator` is per-delivery, owns exactly one fixed pending transition record, and introduces no hidden global cardinality. It arms exact `(NextHop DeviceIdentifier, NextHop MembershipIncarnation, MeshMessageId, immutable deadline)` and accepts only already-authenticated matching next-hop acceptance. Only then does it invoke `CommitSuccessfulForwardingTransition()` and consume one HopLimit. Wrong next hop, wrong incarnation, wrong MessageId, expired evidence and exhausted HopLimit cannot commit. It defines no wire packet and allocates no control family ID.

`ForwardingAttemptLifecycle` maps synchronous forwarding submission failures into the existing route-attempt/replan policies, but every accepted submission remains `AwaitingNextHopAcceptance` regardless of direct-link evidence strength. Only `ForwardingAcceptanceResult::Committed` resolves the route attempt as Delivered.

Intermediate next-hop acceptance transfers forwarding responsibility for one hop. It remains distinct from the end-to-end destination delivery acknowledgement tracked by `DeliveryAcknowledgementTracker`.

## 3.8 Radio / executable direct peers / direct-link evidence

Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is process-local generation-safe link capability only, never Mesh identity or distributed topology identity.

Successful authentication may emit `AuthenticatedDirectPeerBinding` containing authenticated neighbour DeviceIdentifier + MembershipIncarnation plus local RadioIdentifier + RadioPeerHandle. `AuthenticatedDirectPeerBindingTable<96>` retains executable local bindings; exact current incarnation is required. Radio peer invalidation removes executable binding only and never authenticated membership.

`ForwardingSubmissionCoordinator` resolves a validated route's next hop against current authenticated membership and exact-incarnation direct-peer binding before `RadioTransport::Send(peer, ...)`. It never falls back to raw RadioAddress or pre-auth state.

Radio direct-link semantics are explicitly split:
1. send submission/attempt returned,
2. transmission completion proven,
3. peer acknowledgement proven where supported.

`RadioSendResult` carries `RadioDirectLinkEvidence` with `RadioTransmissionCompletion::{Unknown,Completed}` and `RadioPeerAcknowledgement::{Unavailable,Unknown,Acknowledged}`. `RadioTransport` aggregates evidence across every fragment: a logical transfer is Completed only if every fragment is known completed, and Acknowledged only if every fragment is acknowledged.

Consumer-facing observer semantics are explicit and **no misleading completion compatibility alias remains**:
- `IRadioSendAttemptObserver::OnRadioSendAttempted(...)` means the concrete `Send` call returned.
- `RadioObserverSubscriptions::NotifySendAttempted(...)` is the only provider-side send-attempt notification hook on the propagation branch.
- `IRadioTransportMessageObserver::OnRadioTransportSendAttempted(...)` means the complete logical-transfer send attempt returned.
- `RadioSendAttemptedEvent` / `RadioTransportSendAttemptedEvent` carry those same attempt semantics through the optional Event bridge.
- Consumers must inspect `RadioSendResult::Evidence` / `RadioTransportSendResult::LinkResult.Evidence` for stronger completion/acknowledgement facts.

Concrete semantics:
- ESP32 Raw80211 `esp_wifi_80211_tx()` success = submission only; completion unknown, peer ack unavailable.
- ESP32 BLE advertising = queued/submitted bearer work only; no proven per-packet peer acknowledgement.
- NRF24 `RF24::write()` successful unicast = transmission completed + hardware peer acknowledged; successful broadcast = transmission completed + acknowledgement unavailable.

Even `PeerAcknowledged` is direct-link evidence only. It is not next-hop Mesh acceptance, is not a Mesh destination delivery ACK, and cannot consume HopLimit by itself.

## 3.9 Traffic / execution / payload / memory

Protected default traffic capacities: InfrastructureResponse=8, ClockControl=4, GeneralControl=8, Application=8 through injected `IMeshTrafficGovernor`. Control work has finite injected lifetime. Mesh stays decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS belongs in Mesh. Every queue/resource is finite/backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

Locked capacities: MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; protected traffic 8/4/8/8.

Provisional baseline remains ~70,160 B fixed/cardinality + starting stacks before variable payload/control/reassembly/runtime overhead: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`. Direct-peer metadata and whichever finite delivery-ACK capacity a composition selects must be included in the next measured accounting. `ForwardingTransitionCoordinator` contributes one fixed record per already-bounded active forwarding/delivery object; `OutboundDeliveryLifecycle` likewise adds only per-delivery scalar/identity bookkeeping and no new global cardinality.

---

# 4. Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family-registry boundaries.
- Radio bounded logical transfer, peer registry/lifecycle, peer-handle unicast, direct-link evidence aggregation, corrected send-attempt observer/Event semantics.
- ESP32 shared Wi-Fi/Raw80211 PHY coordinator; Raw80211 submission-only direct-link evidence; BLE Radio concrete aligned to send-attempt semantics.
- NRF24 direct-link semantics and `NotifySendAttempted` provider hook.
- State structural realignment.
- Command intrinsic RPC machinery removed and repository/Wiki documentation realigned to asynchronous intent/local disposition; one source IDE-comment cleanup may remain.
- Event provenance/context separation and EVTT v2; distributed identity paused.

Validated Mesh foundations:
- membership tombstones/dedup/MessageId/inbound reservations;
- authenticated membership/retention/lifecycle/liveness/probes;
- pre-auth candidate/authentication resources and authenticated promotion;
- PrimitiveReceiverRegistry<8> and advertised support snapshot;
- MeshRadioRegistry<4>, neighbour discovery and peer cleanup;
- traffic governor and control-work lifetime;
- directed topology, graph-wide storage, freshness and publication policy;
- ResolvedRoute<16>, RouteCache<32>, IRoutingStrategy, revalidation/planner;
- IRouteAttemptPolicy / IRetryPolicy / deadline-aware RouteAttemptCoordinator;
- AuthenticatedDirectPeerBindingTable<96> and DirectPeerLifecycleCoordinator;
- ForwardingSubmissionCoordinator and `ForwardingDirectLinkEvidence::{None,SubmissionAccepted,TransmissionCompleted,PeerAcknowledged}`;
- `CommitSuccessfulForwardingTransition()` as the hop-budget mutation primitive;
- `ForwardingTransitionCoordinator` as the authenticated exact-next-hop acceptance gate before that mutation;
- `ForwardingAttemptLifecycle` preserving accepted Radio work as pending until exact authenticated next-hop Mesh acceptance;
- `DeliveryAcknowledgementTracker<Capacity>` / `DeliveryAcknowledgementCoordinator<Capacity>` for end-to-end destination-framework ACK semantics;
- `OutboundDeliveryLifecycle<AcknowledgementCapacity>` composing route attempts, next-hop acceptance and optional final-destination ACK state under one immutable MessageId/deadline;
- dependency-inverted `IMeshSecurityAuthority` / `IMeshAdmissionPolicy` plus exact `AuthenticatedMeshIdentity`.

---

# 5. Validation record

Earlier green Mesh runs include `33785734451`, `33786121103`, `33786446663`, `33786968405`, `33787189204`, `33789240336`, `33792457888`, `33792536582`, `33793151350`, `33793950787`, `33794119177`, `33797422944`, `33797806648`, `33798395545`, `33800204015`, `33800502633`, `33801088318`, `33801088364`, `33801574428`, `33801574478`, `33804484722`, `33804812963`, `33805171842`, `33805171816`, `33807676687`, `33807676751`, `33808650254`, `33810738804`, `33815259671`, `33845580307`, `33845580185`, `33845944637`, `33845944459`, `33845944550`, `33845944552`, `33848231756`, `33848231773`, `33849228375`, `33849228424`, `33849621816`, `33849621643`, `33849826904`, `33859388270`, `33859388136`.

Earlier Radio green runs include `33793581249`, `33797082636`, `33798291517`, `33799876449`, `33848107862`, `33858866923`. Earlier NRF24 green includes `33847918464`, `33854264847`.

Current Radio observer/evidence realignment:
- `d9e6c325cadaa3c23b7bc83ad82e7d5d205e307c` removed the temporary/misleading provider-side completion compatibility hook entirely.
- `594a6825733a62158b68b87d250d2989efe08b97` renamed the RadioTransport consumer callback/dispatcher to `OnRadioTransportSendAttempted` / `NotifySendAttempted` semantics.
- `07644065808797445c2380db521dab008e038ab1` aligned the optional Radio Event bridge with transport attempt semantics.
- `58396f54e35beb30df8713f0789dc14133d8ba1f` migrated the final native clock test stub.
- `ee10720763fb8ee78be3f349a92e7481331015e3` documents Radio admission vs transmission completion vs peer acknowledgement. Radio run **`33858866923` fully passed** native and ESP32-worker smoke coverage.
- ESP32 Raw80211/BLE and NRF24 propagation branches use `NotifySendAttempted`; ESP32 provider run `33858336799` and NRF24 run `33854264847` passed.

Authenticated forwarding/delivery composition:
- `a9179dda5572c609b2165904ef0e7bb07a3cd6f5` added `ForwardingTransitionCoordinator`.
- Topology Evolution `33859388270` and General CI `33859388136` passed that forwarding transition / ACK / security slice.
- During the 2026-09-04 10:16–10:23 UTC continuation, a proposed `ForwardingAttemptResolution` helper/test was created, then immediately removed after self-review showed `ForwardingAttemptLifecycle` already owned exactly that responsibility. Removal commits: `55be7707f4db39024276cbe498ace26ec736ef70` and `cbe8a7c6256576026752692b1cbc27cfb05e01c3`. No duplicate abstraction remains.
- `a8f24024e00bf04f56671ceb249c49630c52fa78` added sender-side reserve/release operations to `DeliveryAcknowledgementCoordinator` without exposing tracker internals.
- `19cc394787c69b1b63d22e6031c69f8591d77674` added `OutboundDeliveryLifecycle<AcknowledgementCapacity>`.
- `982d0d5b64b7b21e3a6f7bb233578f59d8b11dc1` added composed lifecycle regression coverage; `d269bc79c0d64702ead07b7024273166deb0361f` exposed it through the Mesh umbrella.
- Workflow edits exposed case-sensitive include-path typos during self-review; they were corrected before validation was accepted. Final corrected workflow commit: `c5df67a8f2c84ad0b26dc513b0dbfff635422a06`.
- Topology Evolution run **`33862960895` passed** all topology/routing/forwarding/ACK/security regressions, including the new composed outbound lifecycle and umbrella build.
- General Mesh CI run **`33862960929` passed** the broad host regression suite against the same branch tip.

No Actions-budget symptom has occurred.

---

# 6. Critical boundaries

- Pre-auth state cannot mutate authenticated membership/dedup/liveness.
- Radio peer handles are local capabilities only; their loss never implies member departure.
- Direct executable peer binding is exact-incarnation-qualified and created only after authenticated promotion.
- Radio send-attempt return is not transmission completion unless `RadioDirectLinkEvidence` says so.
- Radio transmission completion/peer acknowledgement are not authenticated next-hop Mesh acceptance.
- Authenticated next-hop Mesh acceptance transfers forwarding responsibility and alone may consume one local HopLimit.
- Next-hop acceptance is not end-to-end destination delivery acknowledgement.
- Mesh destination delivery acknowledgement is not requested-operation completion.
- The originating delivery's final ACK state remains independently pending after intermediate hop acceptance.
- Route/cache state is disposable local optimization and never distributed authority.
- No universal RouteCost, hidden topology threshold or implicit cache eviction policy has been introduced.
- No unresolved Primitive/Event/Group identifier has been allocated.
- Security composition reuses external cryptographic facilities through Mesh-owned dependency inversion and does not invent a handshake wire schema.

---

# 7. Immediate next work

1. Continue wire-neutral security/admission lifecycle composition around `IMeshSecurityAuthority` / `IMeshAdmissionPolicy`, existing bounded candidate/authentication resources and `AdmissionPromotionCoordinator`, without inventing a handshake/control wire schema.
2. Audit measured `sizeof`/capacity impact of newly added direct-peer, liveness and per-delivery lifecycle structures and update the provisional memory accounting where concrete compile-time sizes can be measured without choosing unresolved capacities.
3. Finish the remaining Command source IDE-comment wording cleanup without changing `CommandResult` runtime shape or callback semantics.
4. Keep Event distributed identity and full NodeProfile paused until Simon resolves Event IDs/MessageId and Group identifier semantics.
5. Create/verify `ESPressio-MeshAdapters` propagation branch only when adapter implementation starts.

---

# 8. Chronological UTC implementation log

- 2026-09-03 12:59–17:53 — propagation branches plus System/Primitive/Radio/ESP32/NRF24/State/Command/Event foundations and initial Mesh membership/delivery work.
- 18:12–20:20 — liveness, lifecycle, pre-auth resources, Primitive receivers, Radio peer/discovery lifecycle, traffic governance/control lifetime, liveness probes and advertised support.
- 20:43–21:34 — Security audit, bounded topology/freshness, publication materiality, globally bounded graph and route/cache foundations.
- 21:56–22:55 — routing strategy, retry policies, route revalidation/planner and immutable-deadline attempt orchestration.
- 2026-09-04 06:39–06:49 — authenticated direct-peer binding, peer lifecycle convergence, forwarding submission, explicit hop transition and direct-link evidence boundary.
- 07:24–07:30 — Command documentation realigned from RPC-era language to asynchronous intent/local disposition.
- 07:31–07:39 — wire-neutral delivery acknowledgement tracker/coordinator added and validated.
- 07:39–07:42 — dependency-inverted Mesh security/admission contracts added and validated.
- 08:33–09:34 — Radio send-attempt/evidence semantics audited and realigned across Radio/ESP32/NRF24/Event; misleading completion aliases removed; Radio `33858866923` green.
- 09:35–09:40 — per-delivery `ForwardingTransitionCoordinator` added and validated; Topology Evolution `33859388270` and General CI `33859388136` green.
- 10:16–10:18 — self-review detected that a newly drafted `ForwardingAttemptResolution` duplicated existing `ForwardingAttemptLifecycle`; duplicate source/test deleted immediately before further integration.
- 10:18–10:23 — added sender reserve/release surface on DeliveryAcknowledgementCoordinator and composed `OutboundDeliveryLifecycle<AcknowledgementCapacity>` with regression/umbrella/workflow coverage. Corrected case-sensitive workflow include typos before accepting CI. Topology Evolution `33862960895` and General CI `33862960929` green.
