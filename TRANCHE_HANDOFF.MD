# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE FROZEN / IMPLEMENTATION READY.**

Validated propagation now spans System, Primitive, Radio, ESP32, NRF24, State, Command RPC-removal/documentation realignment, Event provenance, and substantial Mesh membership, delivery, routing, Radio-integration and security/admission foundations. Radio send-attempt, direct-link completion, peer acknowledgement, authenticated next-hop Mesh acceptance, final destination delivery acknowledgement, and requested-operation completion are all explicitly separate concepts.

This is the authoritative implementation handoff for the tranche originating in `ESPressio-Mesh`. Do not alter public/distributed semantics, dependency direction, identity/authentication semantics, locked capacities or Policy without Simon Stuart's explicit decision.

**Unresolved public/wire decisions:** concrete ESPressio `PrimitiveFamilyId` allocations; distributed EventTypeId allocation/derivation; conceptual Event MessageId generation scope/strategy; Group identifier contract. Do not invent any of these.

---

# 1. Branch scope

Primary:
- `ESPressio-Mesh` → `structural_realignment_propagation`

Affected repositories all use `structural_realignment_propagation_ESPressio-Mesh`:
- `ESPressio-System`
- `ESPressio-Primitive`
- `ESPressio-Radio`
- `ESPressio-ESP32`
- `ESPressio-NRF24`
- `ESPressio-State`
- `ESPressio-Command`
- `ESPressio-Event`

`ESPressio-MeshAdapters` remains architecturally required but is not yet branched/implemented. Repository-name correction from Simon: **`ESPressio-Primitive` is singular**.

---

# 2. Mandatory Policy/workflow rules

- Ordinary libraries never call hardware/platform APIs directly; use the appropriate ESPressio abstraction.
- Reuse existing abstractions and avoid duplicate responsibilities.
- No circular dependencies, including optional ones. Stop and ask Simon if one becomes unavoidable.
- Participating manifests/tests/jobs/docs/source references must target matching propagation branches.
- Documentation and IDE-compatible doc comments move with APIs.
- Internal diagnostics use compile-switchable `ESPressio-Logging` after checking its current API.
- Optional callback surfaces use `ESPressio-Observable`; optional Event bridges live with the source subsystem.
- Do not change version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.
- Maintain this handoff after meaningful implementation/validation/decision/correction changes. At full completion offer it to Simon for download, then delete it from Git.
- If Actions begins failing almost immediately due monthly task/job budget exhaustion, record that explicitly and stop treating CI as correctness evidence until reset. **No budget-exhaustion symptom has occurred.**

---

# 3. Frozen architecture

## Layering

- System owns foundational platform-neutral identity/types/services and canonical `DeviceIdentifier`.
- Primitive owns dependency-neutral conceptual-message vocabulary only.
- Command is asynchronous intent, not RPC.
- Event is occurrence semantics.
- State is authoritative fact/replication semantics.
- Radio is opaque direct-link physical/link transport with hop-local packetization/reassembly/retry/evidence.
- Mesh owns membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end delivery. Mesh must not depend on Command/Event/State.
- MeshAdapters sits above Mesh and Command/Event/State.
- Timing owns synchronization math/discipline/quality; Mesh coordinates root/parent; Radio owns direct timestamp mechanics.

## Identity and primitive vocabulary

- `DeviceIdentifier`: exact 16 B, permanent transport-independent, zero invalid, never authentication.
- `MeshIdentifier`: exact nonzero 16 B app-supplied.
- `MembershipIncarnation`: exact nonzero 16 B participation-instance ID.
- `MeshNodeAlias`: uint16 Mesh-local routing handle.
- `RadioIdentifier`: uint8, 0 invalid, 1–254 usable, 255 reserved, incarnation-local.
- `MeshMessageId`: uint64 nonzero monotonic/nonwrapping scoped to authenticated `(DeviceIdentifier, MembershipIncarnation)`; retries/routes/fragments retain it.
- `ProfileGeneration` / `TopologyGeneration`: uint64 nonzero.
- `CanonicalName`: mandatory Mesh-unique printable ASCII 1–32 B, exact case-sensitive comparison.
- `ContractFingerprint`: exact 32 B SHA-256 semantic compatibility digest, never authentication.
- `PrimitiveFamilyId`: uint16; 0 invalid; 0x0001–0x7FFF ESPressio-defined; 0x8000–0xFFFE private; 0xFFFF reserved. Concrete ESPressio IDs remain unallocated.

## Command / Event / State

Command has no intrinsic wait/acceptance/requested-operation completion/result timeout/destination/reply semantics. `CommandResult` means local parser/validation/middleware/dispatch/callback disposition only. Later operation progress/completion belongs to independent Event/State messages, optionally correlated.

Event has EventTypeId, SchemaVersion, conceptual MessageId, priority, payload and optional CorrelationId. Local/Remote origin belongs to dispatch context. No transport HopCount/response/completion semantics. Distributed EventTypeId and Event MessageId generation remain unresolved.

`StateAddress = DeviceIdentifier + StateTypeId`. Local State remains application-owned/non-owningly bound; remote replicas are State-owned. Availability is separate from value. Wire concepts are Publication, Availability, Subscribe, Unsubscribe and Resynchronize; no ReplicaAcknowledgement/generic Disconnect.

## Membership / liveness / tombstones

Admission progression: Unknown → Discovered → Authenticating → Validating → Joining → Active. DeviceIdentifier is never authentication. Direct-neighbour authentication and end-to-end origin authentication are distinct.

Reachability is Unknown / Reachable / Suspect / Unreachable via injected policy; approximate defaults 5 s Suspect / 15 s Unreachable. Valid authenticated evidence may restore Reachable immediately. Full unreachable-record retention defaults to 60 s. Active liveness probes are independently bounded to 4.

Membership tombstones are independently bounded to 64, compact/local, default retention ~5 min, deterministic bounded eviction. Tombstone pressure never weakens authentication/admission.

## Security / admission

`IMeshSecurityAuthority` is Mesh-owned dependency inversion only; Mesh does not depend on ESPressio-Security. It receives a wire-neutral candidate context containing generation-safe candidate handle, local RadioIdentifier/RadioPeerHandle and explicitly untrusted identity claim. It may return only authentication disposition plus exact `AuthenticatedMeshIdentity` on success.

Authentication dispositions are Authenticated / Pending / Retryable / Rejected / ResourceUnavailable / Invalid. Pending retains bounded security work and confers no identity authority.

`IMeshAdmissionPolicy` is independently injected and runs only after exact authenticated identity exists. It performs no cryptography and cannot replace authentication. No security handshake/control-wire schema has been invented.

`AdmissionLifecycleCoordinator` now composes the bounded candidate table, authentication reservation table, injected security authority, injected admission policy and `AdmissionPromotionCoordinator` without adding wire/security semantics. Pending retains the reservation; retryable/security-resource/defer release expensive work and return the candidate to Discovered; definitive rejection removes candidate state; only separately authenticated identity may be promoted into Validating membership. Successful promotion may emit an authenticated direct-peer binding, but retaining that local execution binding remains a separate concern.

## Delivery / dedup / acknowledgements

API destinations are Node / Group / CapabilitySelector / Broadcast. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set and normalize to independent Node deliveries. Broadcast is bounded best-effort flood, one MessageId, no frozen recipient set and no complete-delivery promise.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using uint64 high-water plus 128-position bitmap. Unseen inbound delivery first reserves bounded InProgress state. Definitive disposition commits dedup; retryable/resource-unavailable releases without commit. Old-incarnation traffic cannot mutate current dedup state.

Mesh destination ACK means destination-framework delivery confirmation only. It is distinct from Radio peer acknowledgement, intermediate next-hop acceptance and requested-operation completion. Sender-local pending ACK identity is destination DeviceIdentifier + MembershipIncarnation + MeshMessageId under the immutable delivery deadline.

`DeliveryAcknowledgementTracker<Capacity>` requires explicit finite composition capacity because architecture freezes no universal pending-ACK cardinality. `DeliveryAcknowledgementCoordinator<Capacity>` creates wire-neutral ACK intent only after definitive destination acceptance and applies only authenticated ACKs; it also provides narrow sender reserve/release operations. No ACK wire encoding/family ID is implied.

`OutboundDeliveryLifecycle<AcknowledgementCapacity>` composes route-attempt state, one exact next-hop `ForwardingTransitionCoordinator`, optional final-destination ACK tracking, the same MeshMessageId and one immutable deadline. It owns no payload, route, queue, task, timer, Radio object or wire encoding and invents no global capacity.

## Topology / routing / forwarding

Topology is a globally bounded directed graph; each member is authoritative only for its own outbound set. A→B and B→A are independent. `TopologyGeneration` versions one complete outbound set per authority+incarnation. `MaxTopologyLinks=96` is graph-wide.

Topology stores technology-independent observations and defines no universal `RouteCost`. Publication materiality uses injected `ITopologyPublicationPolicy`. Remote freshness is Fresh / Degraded / Stale / Expired through injected policy; Expired topology cannot originate a route.

Routing uses injected `IRoutingStrategy`. `ResolvedRoute<16>` is a local disposable planning artifact, never source-routing wire state. `RouteCache<32>` is optimization only. Every cache hit/fresh plan is revalidated. Same-route attempts are bounded to 3, distinct routes to 4, all under the immutable delivery deadline.

`RemainingHopLimit` decrements **only** when a successful Mesh forwarding transition is committed. Planning, cache lookup, Radio admission, Radio transmission completion, Radio peer ACK and failed work do not decrement it.

`ForwardingSubmissionCoordinator` resolves a validated route next hop through current authenticated membership + exact-incarnation direct-peer binding before RadioTransport send. `ForwardingAttemptLifecycle` keeps every accepted Radio send pending exact next-hop Mesh acceptance regardless of direct-link evidence. `ForwardingTransitionCoordinator` accepts only authenticated exact next-hop DeviceIdentifier + MembershipIncarnation + MeshMessageId before deadline; only then may `CommitSuccessfulForwardingTransition()` decrement HopLimit.

Intermediate next-hop acceptance transfers forwarding responsibility for one hop and remains distinct from final destination ACK.

## Radio / executable peers / evidence

Radio accepts complete immutable bounded logical transfers to directly reachable Radio peers and owns fragmentation/reassembly/physical retry. `RadioPeerHandle` is generation-safe process-local link capability only, never Mesh identity/distributed topology identity.

`AuthenticatedDirectPeerBindingTable<96>` maps authenticated neighbour incarnation + local RadioIdentifier to executable RadioPeerHandle. Peer invalidation removes executable binding only, never membership.

Radio direct-link semantics are separate: send attempt returned; transmission completion proven; peer acknowledgement proven where supported. `RadioSendResult` carries qualified `RadioDirectLinkEvidence`; RadioTransport aggregates it across fragments.

Observer semantics use `OnRadioSendAttempted` / `OnRadioTransportSendAttempted`; no misleading completion compatibility alias remains. Raw80211 `esp_wifi_80211_tx()` success is submission only. BLE advertising is queued/submitted bearer work only. NRF24 successful unicast `RF24::write()` proves transmission completion + hardware peer ACK; successful broadcast proves transmission completion with ACK unavailable. Even peer ACK is not Mesh next-hop acceptance.

## Traffic / execution / payload / memory

Protected default traffic capacities: InfrastructureResponse 8, ClockControl 4, GeneralControl 8, Application 8 through injected governor. Control work has finite injected lifetime. Mesh remains decomposed into narrow services; one serialized execution domain is acceptable; no direct FreeRTOS belongs in Mesh. All resources/queues are finite and backpressured.

Payload forms: Borrowed Stable / Bounded Owned / Repeatable Serialized Source. Finite size is known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments.

Locked capacities: MaxMeshNodes32; MaxRadiosPerNode4; MaxGroupsPerNode8; MaxPrimitiveReceivers8; MaxActiveApplicationTransmissions8; MaxTopologyLinks96; MaxRouteHops16; DefaultHopLimit16; MaxRouteCacheEntries32; MaxActiveInboundDeliveries8; MaxPendingNeighbourCandidates8; MaxActiveLivenessProbes4; MaxActiveInboundAuthentications4; MaxRecipientsPerTransmission32; MaxMembershipTombstones64; DedupWindow128; MaxSameRouteAttempts3; MaxRoutesAttempted4; protected traffic 8/4/8/8.

Provisional baseline remains ~70,160 B fixed/cardinality + starting stacks before variable payload/control/reassembly/runtime overhead: `DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`. Newly added direct-peer/liveness/per-delivery structures must be included in measured accounting; explicit ACK capacity remains composition-selected.

---

# 4. Current implementation state

Validated lower/platform work:
- System canonical DeviceIdentifier.
- Primitive conceptual IDs/version/fingerprint/family-registry boundaries.
- Radio bounded logical transfer, peer registry/lifecycle, peer-handle unicast, direct-link evidence aggregation, corrected send-attempt observer/Event semantics.
- ESP32 shared Wi-Fi/Raw80211 PHY coordinator, Raw80211 submission-only evidence, BLE Radio alignment.
- NRF24 direct-link semantics.
- State structural realignment.
- Command intrinsic RPC machinery removed and docs/Wiki realigned; one source IDE-comment cleanup may remain.
- Event provenance/context separation and EVTT v2; distributed identity paused.

Validated Mesh foundations:
- membership tombstones/dedup/MessageId/inbound reservations;
- authenticated membership/retention/lifecycle/liveness/probes;
- bounded pre-auth candidate/authentication resources;
- `IMeshSecurityAuthority`, `IMeshAdmissionPolicy`, `AdmissionPromotionCoordinator`, and composed `AdmissionLifecycleCoordinator`;
- PrimitiveReceiverRegistry<8> and advertised support snapshot;
- MeshRadioRegistry<4>, neighbour discovery and peer cleanup;
- traffic governor/control-work lifetime;
- directed topology, graph-wide storage, freshness/publication policy;
- ResolvedRoute<16>, RouteCache<32>, injected routing/revalidation/planner;
- route-attempt/retry policy and deadline-aware coordinator;
- authenticated direct-peer binding table/lifecycle;
- forwarding submission, direct-link evidence classification, exact authenticated next-hop transition, forwarding-attempt lifecycle;
- delivery ACK tracker/coordinator and composed outbound-delivery lifecycle.

---

# 5. Validation record

Key recent green validation:
- Radio run `33858866923` — complete native + ESP32-worker smoke coverage green after send-attempt/evidence cleanup.
- Mesh Topology Evolution `33859388270` + General CI `33859388136` — exact next-hop forwarding transition / delivery-ACK / security contract slice green.
- Mesh Topology Evolution `33862960895` + General CI `33862960929` — composed outbound delivery lifecycle green, including umbrella build.
- Mesh General CI **`33863391792`** — composed admission lifecycle regression and broad host suite green on head `7672ae0479a990cc778e11d74502b48c8417bea1`.
- Topology Freshness `33863391781`, Primitive Support Snapshot `33863391780`, Control Work Lifetime `33863391789` also green on that admission-lifecycle head.

Relevant latest commits:
- `a8f24024e00bf04f56671ceb249c49630c52fa78` — sender reserve/release surface on DeliveryAcknowledgementCoordinator.
- `19cc394787c69b1b63d22e6031c69f8591d77674` — `OutboundDeliveryLifecycle`.
- `982d0d5b64b7b21e3a6f7bb233578f59d8b11dc1` — outbound lifecycle regression.
- `c5df67a8f2c84ad0b26dc513b0dbfff635422a06` — corrected workflow coverage for outbound lifecycle.
- `e38cf770b24ef1c2100d539761bb1dfcb7e1d56a` — `AdmissionLifecycleCoordinator`.
- `9922050b52592f9cf051e520b7b2ef8486121486` — admission lifecycle regression.
- `981d268f19daaca21a62949855ed58d3b904083c` — broad CI coverage.
- `7672ae0479a990cc778e11d74502b48c8417bea1` — Mesh umbrella exposure.

Self-review correction: a proposed `ForwardingAttemptResolution` helper/test was created and immediately deleted after discovering `ForwardingAttemptLifecycle` already owned exactly that responsibility. Delete commits `55be7707f4db39024276cbe498ace26ec736ef70` and `cbe8a7c6256576026752692b1cbc27cfb05e01c3`; no duplicate abstraction remains. Workflow path-case typos encountered during the outbound-lifecycle addition were corrected before CI was accepted as evidence.

No Actions-budget symptom has occurred.

---

# 6. Critical boundaries

- Pre-auth state cannot mutate authenticated membership/dedup/liveness.
- Candidate identity claim is never authenticated identity.
- Security `Pending` retains bounded work but grants no authority.
- Admission policy runs only after authentication.
- Radio peer loss never implies membership departure.
- Direct executable peer binding is exact-incarnation-qualified and local only.
- Radio send attempt is not transmission completion unless qualified evidence says so.
- Radio completion/peer ACK are not authenticated next-hop Mesh acceptance.
- Only authenticated exact next-hop Mesh acceptance may consume one HopLimit.
- Next-hop acceptance is not end-to-end destination ACK.
- Destination ACK is not requested-operation completion.
- Route/cache state is disposable local optimization and never distributed authority.
- No universal RouteCost, hidden topology threshold or implicit cache eviction policy exists.
- No unresolved Primitive/Event/Group identifier has been allocated.
- Security composition defines no handshake/control-wire schema.

---

# 7. Immediate next work

1. Measure concrete `sizeof`/capacity impact of Mesh fixed/cardinality structures (especially authenticated membership/dedup, liveness, direct-peer bindings, candidate/auth reservations, topology/cache and per-delivery lifecycle) and update provisional memory accounting without choosing unresolved capacities.
2. Finish remaining Command source IDE-comment wording cleanup without changing `CommandResult` runtime shape/callback semantics.
3. Continue additional wire-neutral Mesh control/lifecycle composition only where no unresolved Mesh control family ID or wire schema is required.
4. Keep Event distributed identity and full NodeProfile paused until Simon resolves Event IDs/MessageId and Group identifier semantics.
5. Create/verify `ESPressio-MeshAdapters` propagation branch only when adapter implementation starts.

---

# 8. Chronological UTC implementation log

- 2026-09-03 12:59–17:53 — propagation branches plus System/Primitive/Radio/ESP32/NRF24/State/Command/Event foundations and initial Mesh membership/delivery work.
- 18:12–20:20 — liveness, lifecycle, pre-auth resources, Primitive receivers, Radio peer/discovery lifecycle, traffic governance/control lifetime, liveness probes and advertised support.
- 20:43–21:34 — security audit, bounded topology/freshness, publication materiality, globally bounded graph and route/cache foundations.
- 21:56–22:55 — routing strategy, retry policies, route revalidation/planner and immutable-deadline attempt orchestration.
- 2026-09-04 06:39–06:49 — authenticated direct-peer binding, peer lifecycle convergence, forwarding submission, explicit hop transition and direct-link evidence boundary.
- 07:24–07:42 — Command async-intent docs, wire-neutral delivery ACK tracker/coordinator and dependency-inverted Mesh security/admission contracts.
- 08:33–09:34 — Radio send-attempt/evidence semantics audited/realigned across Radio/ESP32/NRF24/Event; misleading completion aliases removed; Radio `33858866923` green.
- 09:35–09:40 — exact authenticated next-hop `ForwardingTransitionCoordinator` added/validated; `33859388270` and `33859388136` green.
- 10:16–10:18 — self-review removed duplicate ForwardingAttemptResolution draft in favor of existing ForwardingAttemptLifecycle.
- 10:18–10:23 — sender ACK reserve/release + composed OutboundDeliveryLifecycle added/validated; workflow case corrections completed before accepting `33862960895` / `33862960929`.
- 10:24–10:28 — composed `AdmissionLifecycleCoordinator` added around bounded candidates/auth reservations + injected security/admission + promotion; pending/retry/resource/defer/reject/authenticated identity semantics covered. General CI `33863391792` green.
