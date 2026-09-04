# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

Validated work now spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership/delivery/routing/security foundations. Radio now explicitly distinguishes synchronous send-attempt observation from qualified direct-link completion/acknowledgement evidence. Mesh continues to keep Radio peer acknowledgement separate from Mesh destination-delivery acknowledgement and from requested-operation completion.

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

Mesh delivery ACK means destination-framework delivery confirmation only. It is not requested-operation completion and is distinct from Radio peer acknowledgement. Sender-local pending-ACK state is keyed by exact destination DeviceIdentifier + MembershipIncarnation + MeshMessageId and uses the immutable delivery deadline. A later incarnation cannot acknowledge an older delivery.

`DeliveryAcknowledgementTracker<Capacity>` requires an explicit finite composition-supplied capacity because architecture does not freeze one universal ACK cardinality. `DeliveryAcknowledgementCoordinator<Capacity>` creates only wire-neutral `DeliveryAcknowledgementIntent` after definitive destination semantic acceptance and applies only already-authenticated ACKs. No ACK wire schema, route, security mechanism or control family ID is implied.

## 3.7 Topology / routing

Topology is a globally bounded directed graph; each member is authoritative only for its own outbound set. A→B and B→A are independent. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide.

Topology stores technology-independent observations and defines no universal `RouteCost`. Publication materiality uses injected `ITopologyPublicationPolicy`. Remote freshness is independent local metadata Fresh / Degraded / Stale / Expired through `ITopologyFreshnessPolicy`; Expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`. `ResolvedRoute<16>` is a local disposable planning artifact, never source-routing wire state. `RouteCache<32>` is optimization only. Every cache hit/fresh plan is revalidated against current membership/topology/freshness/link/policy evidence. Same-route attempts bounded to 3, distinct routes to 4, under one immutable delivery deadline.

`RemainingHopLimit` decrements **only when a successful Mesh forwarding transition is explicitly committed**. It does not decrement during planning, cache lookup, queue admission, Radio submission, Radio transmission completion, Radio peer acknowledgement or failed work. A successful Mesh transition requires Mesh-level evidence that the next Mesh node accepted the forwarded message.

## 3.8 Radio / executable direct peers / direct-link evidence

Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is process-local generation-safe link capability only, never Mesh identity or distributed topology identity.

Successful authentication may emit `AuthenticatedDirectPeerBinding` containing authenticated neighbour DeviceIdentifier + MembershipIncarnation plus local RadioIdentifier + RadioPeerHandle. `AuthenticatedDirectPeerBindingTable<96>` retains executable local bindings; exact current incarnation is required. Radio peer invalidation removes executable binding only and never authenticated membership.

`ForwardingSubmissionCoordinator` resolves a validated route's next hop against current authenticated membership and exact-incarnation direct-peer binding before `RadioTransport::Send(peer, ...)`. It never falls back to raw RadioAddress or pre-auth state.

Radio direct-link semantics are explicitly split:
1. send submission/attempt returned,
2. transmission completion proven,
3. peer acknowledgement proven where supported.

`RadioSendResult` carries `RadioDirectLinkEvidence` with `RadioTransmissionCompletion::{Unknown,Completed}` and `RadioPeerAcknowledgement::{Unavailable,Unknown,Acknowledged}`. `RadioTransport` aggregates evidence across every fragment: a logical transfer is Completed only if every fragment is known completed, and Acknowledged only if every fragment is acknowledged.

**Consumer-facing synchronous observer semantics are now also explicit:**
- `IRadioSendAttemptObserver::OnRadioSendAttempted(...)` means the concrete `Send` call returned.
- `RadioSendAttemptedEvent` means the same thing asynchronously through the optional Event bridge.
- Consumers must inspect `RadioSendResult::Evidence` for any stronger completion/acknowledgement statement.
- A temporary provider-side `NotifySendCompleted(...)` hook remains only to let existing concrete propagation branches migrate to `NotifySendAttempted(...)`; it dispatches **send-attempt semantics only** and must not be treated as a consumer completion contract. Remove it after ESP32 concrete call sites are migrated.

Concrete semantics:
- ESP32 Raw80211 `esp_wifi_80211_tx()` success = submission only; completion unknown, peer ack unavailable.
- ESP32 BLE advertising = queued/submitted bearer work only; no proven per-packet peer acknowledgement.
- NRF24 `RF24::write()` successful unicast = transmission completed + hardware peer acknowledged; successful broadcast = transmission completed + acknowledgement unavailable.

Even `PeerAcknowledged` is direct-link evidence only. It is not a Mesh delivery ACK and cannot consume HopLimit or mean the next Mesh node semantically accepted the Mesh message.

## 3.9 Traffic / execution / payload / memory

Protected default traffic capacities: InfrastructureResponse=8, ClockControl=4, GeneralControl=8, Application=8 through injected `IMeshTrafficGovernor`. Control work has finite injected lifetime. Mesh stays decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS belongs in Mesh. Every queue/resource is finite/backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

Locked capacities: MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; protected traffic 8/4/8/8.

Provisional baseline remains ~70,160 B fixed/cardinality + starting stacks before variable payload/control/reassembly/runtime overhead: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`. Direct-peer metadata and whichever finite delivery-ACK capacity a composition selects must be included in the next measured accounting.

---

# 4. Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family-registry boundaries.
- Radio bounded logical transfer, peer registry/lifecycle, peer-handle unicast, direct-link evidence aggregation, corrected send-attempt observer/Event semantics.
- ESP32 shared Wi-Fi/Raw80211 PHY coordinator; Raw80211 submission-only direct-link evidence; BLE Radio concrete present on propagation branch.
- NRF24 direct-link semantics and new `NotifySendAttempted` provider hook.
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
- `CommitSuccessfulForwardingTransition()` as sole hop-budget mutation;
- `DeliveryAcknowledgementTracker<Capacity>` / `DeliveryAcknowledgementCoordinator<Capacity>`;
- dependency-inverted `IMeshSecurityAuthority` / `IMeshAdmissionPolicy` plus exact `AuthenticatedMeshIdentity`.

---

# 5. Validation record

Earlier green Mesh runs include `33785734451`, `33786121103`, `33786446663`, `33786968405`, `33787189204`, `33789240336`, `33792457888`, `33792536582`, `33793151350`, `33793950787`, `33794119177`, `33797422944`, `33797806648`, `33798395545`, `33800204015`, `33800502633`, `33801088318`, `33801088364`, `33801574428`, `33801574478`, `33804484722`, `33804812963`, `33805171842`, `33805171816`, `33807676687`, `33807676751`, `33808650254`, `33810738804`, `33815259671`, `33845580307`, `33845580185`, `33845944637`, `33845944459`, `33845944550`, `33845944552`, `33848231756`, `33848231773`, `33849228375`, `33849228424`, `33849621816`, `33849621643`, `33849826904`.

Earlier Radio green runs include `33793581249`, `33797082636`, `33798291517`, `33799876449`, `33848107862`. Earlier NRF24 green includes `33847918464`.

Current Radio observer realignment:
- `9f9560a98b991c8383caa8174ac5e4ce2f3d86cf` — add `IRadioSendAttemptObserver` / `OnRadioSendAttempted` and attempt-only dispatcher semantics.
- `fffca22eb644012683bf9cdfa910076401d8a11d` — rename optional Events to `RadioSendAttemptedEvent` / `RadioTransportSendAttemptedEvent`.
- `2de0592aba1e0ea7cffcf5f93e9dc49695bf2aa1` — align Radio Event bridge.
- `8cc8fac0a3f46642ea9344389e7d228fdeaec93c` — add send-attempt observer regression.
- `83619de85029f4ae4f8c2b0aed7aaaf453438a20` — add CI coverage. First run `33854226234` exposed stale `IRadioSendObserver` references in `radio_transport_tests.cpp`; native job failed at compile. This was a source/test migration regression, not task-budget exhaustion.
- `2c7a087926d055bc9241d22f855421b228c9407d` — migrate transport tests to the new observer surface. Radio run `33854438612`: native job completed **success**, including transport, direct-link evidence, send-attempt observer, peer-registry and clock-sync regressions. ESP32-worker job was still running at the last handoff update; verify final conclusion before claiming whole-run green.
- `321297b6c39f311123bb3b334064251ae0dbafc0` — NRF24 uses `NotifySendAttempted` directly. NRF24 CI run **`33854264847` passed**.

No Actions-budget symptom has occurred.

---

# 6. Critical boundaries

- Pre-auth state cannot mutate authenticated membership/dedup/liveness.
- Radio peer handles are local capabilities only; their loss never implies member departure.
- Direct executable peer binding is exact-incarnation-qualified and created only after authenticated promotion.
- Radio send-attempt return is not transmission completion unless `RadioDirectLinkEvidence` says so.
- Radio transmission completion/peer acknowledgement are not Mesh destination-delivery acknowledgement.
- Mesh destination-delivery acknowledgement is not requested-operation completion.
- Route/cache state is disposable local optimization and never distributed authority.
- No universal RouteCost, hidden topology threshold or implicit cache eviction policy has been introduced.
- No unresolved Primitive/Event/Group identifier has been allocated.
- Security composition reuses external cryptographic facilities through Mesh-owned dependency inversion and does not invent a handshake wire schema.

---

# 7. Immediate next work

1. Verify final ESP32-worker result for Radio run `33854438612`.
2. Migrate ESP32 Raw80211 and BLE concrete provider call sites from temporary `NotifySendCompleted` hook to `NotifySendAttempted`, then remove the temporary compatibility hook from `RadioObserverSubscriptions`; validate ESP32 + Radio branches.
3. Rename the **RadioTransport consumer-facing synchronous callback** from `OnRadioTransportSendCompleted` to attempt/submission semantics and realign optional Event bridge/Event naming completely; do not confuse aggregate `LinkResult.Evidence` with completion of the API operation.
4. Keep Mesh forwarding transition/HopLimit commitment driven only by Mesh-level next-node acceptance evidence.
5. Continue wire-neutral security/admission composition and bounded traffic/control work only where no unresolved wire IDs are required.
6. Finish remaining Command source-comment wording cleanup.
7. Keep Event distributed identity and full NodeProfile paused until Simon resolves Event IDs/MessageId and Group identifier semantics.
8. Create/verify `ESPressio-MeshAdapters` propagation branch only when adapter implementation starts.

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
- 08:33–08:39 — Radio send observation semantics audited across Radio/ESP32/NRF24. Added `IRadioSendAttemptObserver`, renamed optional send Events, aligned Event bridge, added regression, migrated NRF24 provider to the new hook. Initial Radio run `33854226234` exposed stale native test references; corrected in `2c7a0879...`. Native portion of replacement run `33854438612` passed all regressions; ESP32-worker still running at this exact handoff update. NRF24 run `33854264847` green.
