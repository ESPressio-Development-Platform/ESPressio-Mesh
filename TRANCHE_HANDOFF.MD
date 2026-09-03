# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — SYSTEM / PRIMITIVE / RADIO / ESP32 / NRF24 / STATE / COMMAND RPC-REMOVAL / EVENT PROVENANCE VALIDATED; MESH AUTHENTICATED MEMBERSHIP, DELIVERY, RETENTION, LIVENESS, LIFECYCLE, PRE-AUTH ADMISSION, AUTHENTICATED PROMOTION, EXTERNAL PRIMITIVE RECEIVERS, RADIO-IDENTIFIER/DISCOVERY BINDING AND PROTECTED TRAFFIC GOVERNANCE VALIDATED; RADIO GENERATION-SAFE PEER HANDLES ARE NOW INTEGRATED INTO RADIOTRANSPORT.**

This file is the current absolute truth for the implementation tranche originating in `ESPressio-Mesh`. The architecture below is frozen. Implementation may choose bounded internal mechanics only where doing so does not alter public/distributed semantics, ownership, dependency direction, wire compatibility, locked bounds, authentication/identity semantics or Policy. If a genuine architectural omission/conflict is exposed, stop and ask Simon Stuart. Concrete ESPressio-defined `PrimitiveFamilyId` numeric allocations and the distributed EventTypeId assignment contract remain explicit decision boundaries and MUST NOT be invented autonomously.

---

# 1. Scope and branches

Primary branch:

- `ESPressio-Mesh`: `structural_realignment_propagation`

Affected propagation branches created:

- `ESPressio-System`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Primitive`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Radio`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-ESP32`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-NRF24`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-State`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Command`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Event`: `structural_realignment_propagation_ESPressio-Mesh`

`ESPressio-MeshAdapters` exists architecturally but has not yet been implemented in this tranche; create/verify its matching propagation branch when adapter work begins.

Authoritative correction from Simon: repository name is **`ESPressio-Primitive` (singular)**. Any Policy spelling `ESPressio-Primitives` is a typo.

---

# 2. Mandatory Policy rules

- Ordinary libraries MUST NOT call platform/hardware APIs directly. Portable abstractions belong in the correct ESPressio abstraction library; hardware concretes belong in platform/technology repositories.
- Existing ESPressio abstractions MUST be reused rather than reimplemented locally.
- Circular dependencies, including optional ones, MUST be avoided. If one appears necessary, stop and ask Simon.
- Cross-library propagation remains isolated to the branches above. Participating dependency references in manifests, workflows/tests, examples, docs and source must target matching propagation branches.
- Public-facing discrete components should receive useful Arduino IDE and PlatformIO demos where appropriate.
- Added/changed APIs require meaningful IDE-compatible documentation comments and documentation revalidation.
- If Actions starts failing almost immediately because the organization task/job budget is exhausted, record that explicitly and stop treating CI as correctness evidence until reset; switch to recursive self-review. **No budget-exhaustion symptom has occurred.**
- Internal diagnostics should use ESPressio-Logging and remain compile-switchable; inspect the current Logging API before instrumentation.
- Optional callback surfaces should use ESPressio-Observable multi-observer semantics. Optional Observable→Event bridges live with their source subsystem.
- Keep this `TRANCHE_HANDOFF.MD` on the Mesh operative branch while incomplete. Update after meaningful branch/dependency/code/validation/decision/correction changes. At full completion offer the final handoff to Simon for download, then delete it from Git.
- Do not alter version numbering merely because of trench work.
- No legacy-preservation work unless Simon explicitly requests it.

---

# 3. Frozen architecture

## 3.1 Layering

- `ESPressio-System`: foundational platform-neutral identities/types/services; owns canonical `DeviceIdentifier`.
- `ESPressio-Primitive`: common conceptual-message vocabulary only (`ConceptualMessageId`, `CorrelationId`, `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, `ContractFingerprint`, minimal helpers/contracts).
- `ESPressio-Command`: asynchronous intent.
- `ESPressio-Event`: occurrence semantics.
- `ESPressio-State`: authoritative fact/replication semantics.
- `ESPressio-Radio`: opaque direct-link physical/link transport and link-local mechanics.
- `ESPressio-Mesh`: membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end delivery. Mesh must not depend on Command/Event/State.
- `ESPressio-MeshAdapters`: optional Mesh↔Command/Event/State integration above both sides.
- `ESPressio-Timing`: clock mathematics/discipline/quality; Mesh owns global root/parent coordination, Radio direct-link timestamp mechanics.

## 3.2 Identity / fixed types

- `DeviceIdentifier`: exact 16 B, nonzero valid, System-owned, transport-independent, not authentication.
- `MeshIdentifier`: exact nonzero 16 B application-supplied Mesh identity.
- `MembershipIncarnation`: exact nonzero 16 B, cryptographically strong random per genuine participation lifetime; partition/radio restart does not inherently change it; FastRejoin may retain it only with authenticated continuity evidence.
- `MeshNodeAlias`: uint16 Mesh-local routing handle.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; node-local, unrecycled within incarnation.
- `MeshMessageId`: uint64, zero invalid, monotonic non-wrapping and scoped by authenticated Source DeviceIdentifier + MembershipIncarnation. Fresh independent Node delivery gets fresh ID; retries/routes/fragments retain it; selective multicast gets one per recipient; Broadcast retains one across flood.
- `ProfileGeneration`, `TopologyGeneration`: uint64 nonzero, incarnation-scoped, first value 1, never wrap.
- Default `CapabilityMask`: uint64.
- `CanonicalName`: 1–32 printable ASCII bytes, no NUL/control, no leading/trailing spaces, exact case-sensitive uniqueness; semantic storage = 1 length byte + 32 backing bytes.
- `ContractFingerprint`: exact 32-B SHA-256 semantic compatibility digest, never authentication.

## 3.3 Primitive family vocabulary

`PrimitiveFamilyId` exact uint16:

- 0x0000 invalid
- 0x0001–0x7FFF ESPressio-defined, individually allocated centrally in Primitive
- 0x8000–0xFFFE application/private
- 0xFFFF reserved

Protocol compatibility uses bounded version ranges and normally selects highest mutual version. Family fingerprint covers family semantics, not every application schema. Supports / Compatible / Reachable / Authorized are independent.

**Concrete ESPressio family IDs remain UNALLOCATED.**

## 3.4 Command

Command is asynchronous intent, not RPC. Core conceptual data is identity/version/correlation/payload. No intrinsic waiting, acceptance response, operation completion, result, timeout, destination/route or reply semantics. A local handler disposition says only what local framework/handler did with the invocation, never whether the requested real-world operation completed.

## 3.5 Event

Event is occurrence; transport is travel; local dispatch is processing. Event has no transport address or HopCount. Local/Remote origin belongs to `EventDispatchContext`, not Event object state. EventPriority is local/application policy, not Mesh QoS. Event is one-way and has no intrinsic ACK/response/completion/RPC contract. Remote Event is not automatically re-forwarded.

Frozen intrinsic metadata is EventTypeId, SchemaVersion, conceptual MessageId, EventPriority, payload and optional CorrelationId. Stable distributed EventTypeId assignment remains unresolved; do not equate existing stable-name FNV transport TypeID with it without Simon.

## 3.6 State

`StateAddress = DeviceIdentifier + StateTypeId`. Local State remains application-owned and non-owningly bound/indexed/observed; remote replicas State-owned. State is read-only outside authority; Command represents mutation intent. Availability distinct from value: Available/Stale/Unavailable/Expired with reasons including SourceUnbound and locally derived SourceUnreachable. Duplicate local authority rejected. `NotifyChanged` advances revision/observation/propagation even when equality says equivalent. Retain preserves inactive lineage; Discard removes lineage. Observer lifecycle/value/availability semantics are bounded and same-State non-reentrant. Protocol concepts: Publication, Availability, Subscribe, Unsubscribe, Resynchronize and reciprocal subscription results. No ReplicaAcknowledgement or generic Disconnect. StateEpoch independent from MembershipIncarnation. StateObserverEventBridge lives in State. MeshAdapters validates StateAddress.DeviceIdentifier against authenticated Mesh source.

## 3.7 Primitive receivers / profile support

Mesh has its own control Primitive family. External families are opaque and terminate in exactly one active receiver per PrimitiveFamilyId. Unsupported family is local disposition and does not negate successful Mesh delivery. Receivers receive MeshReceiveContext, selected PrimitiveProtocolVersion and bounded opaque payload. Heavy work must be delegated out of Mesh receive path. Registration bounded/deterministic/lifetime-safe.

Authenticated NodeProfile includes DeviceIdentifier, CanonicalName, CapabilityMask, bounded Groups, primitive support descriptors and ProfileGeneration. Support descriptors carry family ID, supported version range, family fingerprint and exposure. Family support does not normally gate Mesh membership.

## 3.8 Membership / security / liveness

Admission: Unknown → Discovered → Authenticating → Validating → Joining → Active. Cheap compatibility checks precede expensive crypto. Any Active member may sponsor; sponsor is not leader. MembershipState and ReachabilityState distinct. Authenticated Leave authoritative; reachability expiry local only. Security/admission behind injectable abstractions such as IMeshSecurityAuthority / IMeshAdmissionPolicy. Direct-neighbour and end-to-end origin authentication distinct. Old-incarnation traffic must never mutate current authority/dedup state.

Reachability hysteresis is **Reachable → Suspect → Unreachable** via injected liveness policy. Approximate baseline floors 5 s before Suspect, 15 s before Unreachable, not universal constants. Any valid authenticated Mesh evidence may restore Reachable immediately. Probes only when passive/control evidence insufficient.

## 3.9 Tombstones

Default `MaxMembershipTombstones=64`, independent from `MaxMeshNodes=32`. Tombstone contains only DeviceIdentifier, MembershipIncarnation, local monotonic retention deadline and disposition: LocallyForgotten / SupersededIncarnation / AuthoritativeLeave. No profile/topology/routes/radios/payload/dedup.

Default retention ≈5 min. Insert: purge expired, update exact, use free slot, otherwise deterministic eviction: oldest LocallyForgotten first, then SupersededIncarnation, then AuthoritativeLeave; tie by deadline then stable identities. Nothing unevictable. Early eviction loses continuity optimization only and never weakens authentication/admission.

## 3.10 Delivery / dedup

Destinations: Node, Group, CapabilitySelector, Broadcast. Group/CapabilitySelector resolve once into frozen bounded recipient sets then N independent Node deliveries. Selective multicast shares immutable payload backing/aggregate deadline but each recipient has independent MeshMessageId/outcome. Ordinary wire destination Node or Broadcast. Broadcast best-effort controlled flood, unacknowledged, no frozen recipient set or complete-to-all promise, one MeshMessageId through flood. Delivery ACK means destination Mesh delivery only, not semantic execution.

Per authenticated source DeviceIdentifier + MembershipIncarnation retain HighestObservedSequence + compact sliding bitmap. Default 128 positions, minimum configurable 32. Skipped lower sequences remain admissible while within window. Seen = Duplicate; below retained floor = TooOld and never delivered upward. Dedup occurs only after sufficient authentication and incarnation validation. Unseen delivery reserves bounded **InProgress** state before semantic receiver handoff; definitive disposition commits dedup; TemporarilyUnavailable/ResourceUnavailable releases without commit. Concurrent route/radio copies cannot both dispatch upward. Dedup is never silently disabled under pressure. Duplicate tracked unicast/selective multicast may regenerate required delivery receipt without upper-layer redelivery; Broadcast duplicates suppress without ACK.

## 3.11 Topology / routing / retries

Mesh owns orchestration, Radio selected next-hop execution. Injectable IRoutingStrategy / IRouteAttemptPolicy / IRetryPolicy. Directed link state; MaxTopologyLinks 96. TopologyGeneration versions complete semantic outbound-link set and advances only on material published changes. Route cache 32, max route hops 16, default hop limit 16. Adaptive hop-by-hop forwarding; RemainingHopLimit decrements only on successful Mesh forwarding transition. Radio retry Radio-owned. Default Mesh safety ceilings: 3 same-route attempts, 4 distinct routes; attempts only if remaining immutable deadline plausibly permits completion.

## 3.12 Radio / ESP32 shared PHY

Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer/handle. MaximumLogicalTransferSize finite and distinct from physical MTU. Admission immediate; completion asynchronous. Receive yields complete opaque logical transfer + Radio-owned peer/address observations and qualified timing metadata. Radio never interprets DeviceIdentifier, MembershipIncarnation, MeshMessageId, PrimitiveFamilyId or Mesh destination/routing. Concrete Radio owns bounded fragmentation/reassembly/physical retry. Precision timing uses separate protected path.

Radio may create a generation-safe peer handle during discovery before authentication. Mesh later binds that handle to authenticated DeviceIdentifier, MembershipIncarnation and RadioIdentifier before ordinary routing may use it. Handles are Radio-owned, process-local and generation-safe: stale handles fail deterministically and can never resolve to later slot occupants. They are never distributed or persisted as Mesh identity.

ESP32 ordinary Wi-Fi and Raw80211 share one integrated PHY coordinated by ESPressio-ESP32. Raw follows effective Wi-Fi channel by default, never disruptively retunes an incompatible active service, and reports Busy/restricted instead. Channel/tx-power/power-save global state is coordinator-owned; Mesh has no channel semantics.

## 3.13 Clock / traffic / execution / payload / persistence

No configured permanent clock roles. Timing owns synchronization mathematics/quality; Radio direct timestamp mechanics; Mesh root election/parent coordination. Election quality-first with DeviceIdentifier final deterministic tie-break. Precision T1/T2/T3/T4 bypasses ordinary Mesh queues/fragmentation.

Injected IMeshTrafficGovernor with protected default capacities Infrastructure=8, Clock=4, General=8, Application=8. Application saturation cannot consume Mesh-survival reserves; mandatory ACKs remain serviceable. All queues finite.

Mesh decomposes into narrow services, not one giant worker. Responsibility does not imply a task. Prefer ESPressio Task/PrecisionThread; no direct FreeRTOS in Mesh. One serialized Mesh execution domain is acceptable baseline.

Payload forms: Borrowed Stable Payload / Bounded Owned Payload / Repeatable Serialized Source. Finite size known before acceptance. Bytes immutable across retries/routes/fragments. Selective multicast may share immutable backing. Adapters own family serialization; Mesh envelope/end-to-end delivery; Radio link packetization.

Persistence separately injected/composed. Policies/config changing distributed authoritative truth contribute recursively to MeshSignature via stable identifiers/versions/config signatures, never RTTI/compiler/layout/function addresses. Operational local mechanisms normally excluded.

## 3.14 Locked default capacities / planning

- MaxMeshNodes 32
- MaxRadiosPerNode 4
- MaxGroupsPerNode 8
- MaxPrimitiveReceivers 8
- MaxActiveApplicationTransmissions 8
- MaxTopologyLinks 96
- MaxRouteHops 16
- DefaultHopLimit 16
- MaxRouteCacheEntries 32
- MaxActiveInboundDeliveries 8
- MaxPendingNeighbourCandidates 8
- MaxActiveLivenessProbes 4
- MaxActiveInboundAuthentications 4
- MaxRecipientsPerTransmission 32
- MaxMembershipTombstones 64
- DeduplicationWindowBits 128
- MaxSameRouteAttempts 3
- MaxRoutesAttempted 4
- protected capacities 8 / 4 / 8 / 8
- unreachable full-member retention 60 s
- tombstone retention ≈5 min

Provisional fixed/cardinality metadata ≈60,160 B plus starting task stacks ≈10,000 B = ≈70,160 B before variable payload/control/reassembly/runtime overhead. Ordinary payload planning ≈`20 * Papp`.

`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`

Concrete logical/control/reassembly byte capacities remain finite and measured.

**Architecture audit: PASS / IMPLEMENTATION READY.**

---

# 4. Current implementation state

## Validated lower/platform libraries

- **System**: canonical exact-16-B DeviceIdentifier validated.
- **Primitive**: family/version/message/correlation/fingerprint vocabulary validated; concrete family IDs still unallocated.
- **Radio**: bounded direct-link logical transfer; 4096-B generic ceiling, four reassembly slots, 32 recent completions. Compact 4-B `RadioPeerHandle` + fixed-capacity `RadioPeerRegistry` remain Radio-owned and generation-safe. `RadioTransport` now owns/integrates the peer registry: completed inbound logical transfers are delivered only after source peer observation succeeds and carry `SourcePeer`; direct unicast has a preferred `Send(RadioPeerHandle, ...)` path; low-level explicit `IRadio + RadioAddress` send remains for provider/broadcast mechanics. Interface removal and transport Stop invalidate peer handles; stale handles fail with `InvalidPeer`. If peer capacity is exhausted, completed inbound transfer is dropped without committing recent-transfer suppression so a later retry can succeed after resources recover. Radio run `33797082636` passed native transport/peer/clock regressions and ESP32 worker/Event/clock smoke.
- **ESP32**: WiFiPhyCoordinator/shared-PHY Raw80211 coexistence validated; run `33764387563` passed.
- **NRF24**: 4096-B logical transfer over 32-B physical payload; direct clock preserved; run `33764610171` passed.
- **State**: structurally complete/validated; runs `33778512159` and `33778720013` passed.
- **Command**: intrinsic RPC/request-response/timeout/reply model removed; Primitive-backed bounded CommandMessage; run `33780014015` passed. Local `CommandResult` naming/completion wording remains cleanup only.
- **Event**: provenance/context separation validated, EVTT v2, no HopCount semantics, explicit EventDispatchContext through listeners/observers/threads, remote Event not auto-forwarded; run `33785167779` passed all jobs. Stable distributed EventTypeId remains unresolved.

## Mesh core — validated foundations

Implemented on `structural_realignment_propagation`:

- `ReachabilityState`: Unknown / Reachable / Suspect / Unreachable.
- `MembershipTombstoneTable<64>`: bounded compact local incarnation history, monotonic retention, exact refresh and frozen deterministic eviction.
- `DeduplicationWindow<128>`: 24-byte committed dedup core (uint64 high-water + 128-bit bitmap), unseen gaps admissible, duplicates/TooOld explicit.
- `MeshMessageIdGenerator`: starts at 1, monotonic/non-wrapping, fails after uint64 max, supports explicit authenticated-continuity high-water restoration only.
- `InboundDeliveryReservationTable<8>`: bounded InProgress exclusion keyed by authenticated Source + MembershipIncarnation + MeshMessageId.
- `AuthenticatedMembershipTable<32>`: fixed-capacity authenticated participation records for Validating/Joining/Active states. Each record owns its source/incarnation-scoped committed delivery dedup window. A different incarnation for the same DeviceIdentifier is rejected as `ConflictingIncarnation`; replacement is deliberately not implicit.
- `InboundDeliveryCoordinator`: exact authenticated-membership lookup → committed dedup classification → InProgress reservation, with explicit definitive commit vs retryable release.
- `MembershipRetentionCoordinator`: policy-decided full record retirement to compact AuthoritativeLeave / SupersededIncarnation / LocallyForgotten tombstones; tombstone recorded before full record release.
- `IMeshLivenessPolicy` + `DefaultMeshLivenessPolicy`: thresholds are injected policy, defaulting to the architecture baseline 5 s Suspect / 15 s Unreachable.
- `MembershipLivenessTracker<32>`: exact authenticated-evidence timestamps, immediate Reachable restoration, policy-driven degradation, bounded 60 s full-record-retention eligibility without autonomous retirement.
- `MembershipLifecycleCoordinator`: composes liveness with retention and performs LocallyForgotten retirement only after configured full-record retention expires.
- `PendingNeighbourCandidateTable<8>`: bounded pre-authentication Discovered/Authenticating candidates bound to `RadioIdentifier + RadioPeerHandle`; claimed DeviceIdentifier/MembershipIncarnation remain explicitly untrusted hints. Same claim over another Radio peer is a distinct candidate.
- `InboundAuthenticationReservationTable<4>`: bounded expensive-authentication execution slots keyed by candidate handle, with no authenticated authority.
- `AdmissionPromotionCoordinator`: caller supplies identity established externally by injected security/admission. The authenticated DeviceIdentifier + MembershipIncarnation, never the untrusted candidate claim, is promoted to `Validating` membership with `Reachable` initial liveness.
- `PrimitiveReceiverRegistry<8>`: fixed-array heap-free external PrimitiveFamilyId registry, generation-safe registration, version-range checking, ContractFingerprint/exposure descriptors and short bounded dispatch through `MeshReceiveContext` + borrowed payload. No Mesh family number allocated.
- `MeshRadioRegistry<4>`: maps process-local `IRadio*` to incarnation-scoped `RadioIdentifier`; identifiers allocate monotonically from 1 and are never reused during one MembershipIncarnation even if interfaces are removed. `ResetForNewIncarnation()` is the only allocation reset.
- `NeighbourDiscoveryCoordinator`: narrow Radio→Mesh bridge which consumes `RadioTransportMessageView::SourcePeer` plus a registered ingress Radio and untrusted parsed claim, then records the claim in bounded pre-auth storage. Radio peer evidence establishes link provenance only, never identity authority.
- `IMeshTrafficGovernor` + `DefaultMeshTrafficGovernor`: independent protected fixed capacities InfrastructureResponse=8, ClockControl=4, GeneralControl=8, Application=8. Reservations are generation-safe; application saturation cannot consume control reserves. The governor owns admission only, not scheduling/queues/wire semantics.
- Umbrella header exports all foundations.

Validation:

- run `33785734451`: types + tombstones + dedup passed.
- run `33786121103`: message-ID generator added, all passed.
- run `33786446663`: InProgress reservation added, all passed.
- run `33786968405`: authenticated membership + inbound delivery coordinator added, all passed.
- run `33787189204`: membership-retention coordinator added, all passed.
- run `33789240336`: policy-driven liveness added, all passed.
- run `33792457888`: membership lifecycle and bounded admission resources passed.
- run `33792536582`: self-contained liveness-header correction passed.
- run `33793062493`: first Primitive receiver-registry CI attempt failed only because umbrella compile commands lacked the new Primitive include directory; no budget symptom.
- run `33793151350`: corrected Primitive include paths; all Mesh regressions passed.
- Radio run `33793581249`: peer-registry native regression plus existing transport/clock and ESP32 smoke passed.
- Mesh run `33793950787`: Radio-peer-bound admission resources plus authenticated admission-promotion regression passed with every prior host regression.
- Mesh run `33794119177`: handoff-only state reconciliation also passed all host regressions.
- Radio run `33797082636`: integrated RadioTransport peer-handle receive/send path passed both native and ESP32 jobs.
- Mesh run `33797422944`: MeshRadioRegistry + peer-bound NeighbourDiscoveryCoordinator passed every host regression.
- Mesh run `33797806648`: protected traffic-governor regression compiled and ran successfully with every prior regression; workflow cleanup was still completing when this handoff update was written, with no failing test/build step and no Actions-budget symptom.

Important implementation boundaries:

- Authenticated membership contains only already-authenticated records. Discovery/pre-authentication candidates cannot affect authenticated dedup/liveness state.
- Liveness accepts only caller-authenticated exact incarnation evidence and never authenticates packets itself.
- Pending candidate claimed identities are untrusted hints. `AdmissionPromotionCoordinator` takes separately established authenticated identity and therefore does not promote the untrusted claim by implication.
- Radio peer handles are Radio-owned and generation-safe. Mesh stores them only as link-local bindings beside `RadioIdentifier`; they never become Mesh identity.
- `RadioTransport` peer-table pressure is explicit: it does not expose raw RadioAddress upward as a fallback identity and does not poison retry/dedup suppression when peer admission fails.
- `MeshRadioRegistry` owns only incarnation-scoped numbering; it does not own/start Radio interfaces or Radio peer state.
- `DefaultMeshTrafficGovernor` protects classes independently; higher services must classify mandatory delivery receipts/ACKs as InfrastructureResponse rather than Application.
- `PrimitiveReceiverRegistry` still cannot reserve the Mesh control family from external registration until Simon explicitly approves a concrete central PrimitiveFamilyId allocation. No family ID was invented.

---

# 5. Immediate next work

1. Add bounded Radio-peer invalidation observation if needed so Mesh can deterministically purge pre-auth/link bindings when a Radio peer handle is explicitly invalidated or an interface is removed, using ESPressio-Observable rather than ad-hoc single callbacks.
2. Continue admission service composition around injected `IMeshSecurityAuthority` / `IMeshAdmissionPolicy`; do not implement cryptography in Mesh.
3. Begin authenticated profile support in separable pieces that do not require inventing the unresolved Group identifier contract; primitive-support advertisement can be derived from the active Primitive receiver registry.
4. Introduce bounded control-work lifetime policy before control queues are implemented; all control work must remain finite-lifetime and governor-protected.
5. Reconcile Command local `CommandResult` naming/completion wording without reintroducing RPC semantics.
6. Keep Event distributed EventTypeId work paused pending Simon's explicit identity-contract decision.
7. Continue topology → routing → traffic governance queue integration → clock coordination → adapters incrementally.

---

# 6. Chronological UTC work log

- **2026-09-03T12:59:05Z** — Simon authorized implementation. Created Mesh primary propagation branch and initial System/Primitive/Radio propagation branches; created authoritative handoff.
- **2026-09-03T13:13:44Z** — Canonical System DeviceIdentifier and Primitive vocabulary/registry boundaries implemented/validated; no family IDs allocated.
- **2026-09-03T13:22:31Z** — Created ESP32/NRF24/State propagation branches; repinned dependencies; added initial Mesh types/limits.
- **2026-09-03T13:47:30Z–14:03:32Z** — Radio direct-link logical-transfer realignment plus ESP32 shared-PHY and NRF24 propagation validated (`33763062738`, `33764387563`, `33764610171`).
- **2026-09-03T14:09:00Z–16:27:18Z** — State structural migration completed and validated (`33778512159`, `33778720013`).
- **2026-09-03T16:40:00Z–16:43:07Z** — Command/Event propagation branches; Command RPC removal + bounded Primitive-backed CommandMessage validated (`33780014015`).
- **2026-09-03T16:45:00Z–17:32:26Z** — Event Primitive vocabulary/provenance separation, EVTT v2, explicit EventDispatchContext and docs/examples reconciled; final Event run `33785167779` passed.
- **2026-09-03T17:36:00Z–17:38:18Z** — Corrected Mesh ReachabilityState to Suspect; implemented bounded tombstones and DeduplicationWindow; run `33785734451` passed.
- **2026-09-03T17:42:18Z** — Added non-wrapping MeshMessageIdGenerator; run `33786121103` passed.
- **2026-09-03T17:44:00Z–17:45:35Z** — Added fixed-capacity InboundDeliveryReservationTable; run `33786446663` passed.
- **2026-09-03T17:49:00Z–17:50:55Z** — Added AuthenticatedMembershipTable and InboundDeliveryCoordinator; run `33786968405` passed.
- **2026-09-03T17:52:00Z–17:53:20Z** — Added MembershipRetentionCoordinator; run `33787189204` passed.
- **2026-09-03T18:12:00Z–18:13:58Z** — Added policy-driven MembershipLivenessTracker and replaceable IMeshLivenessPolicy; run `33789240336` passed.
- **2026-09-03T18:44:00Z–18:46:30Z** — Added MembershipLifecycleCoordinator and bounded pre-auth candidate/authentication resources; run `33792457888` passed.
- **2026-09-03T18:46:53Z–18:47:20Z** — Made MembershipLiveness.hpp self-contained; run `33792536582` passed; identified missing Radio peer-handle contract.
- **2026-09-03T18:49:00Z–18:53:30Z** — Added PrimitiveReceiverRegistry without family allocation. Initial run `33793062493` exposed missing Primitive include path; corrected run `33793151350` passed.
- **2026-09-03T18:56:00Z–18:59:00Z** — Implemented Radio-owned compact generation-safe `RadioPeerHandle` and bounded `RadioPeerRegistry`; Radio run `33793581249` passed both jobs.
- **2026-09-03T18:59:00Z–19:01:40Z** — Bound Mesh pending candidates to `RadioIdentifier + RadioPeerHandle`; added `AdmissionPromotionCoordinator`; Mesh run `33793950787` passed every current host regression.
- **2026-09-03T19:31:00Z–19:34:30Z** — Integrated RadioPeerRegistry directly into RadioTransport. Complete inbound transfers now carry a valid Radio-owned SourcePeer, peer saturation drops without recent-transfer commit, handle-based direct unicast added, and Stop/interface removal invalidate peers. Regression verifies source-handle resolution, handle-based reply, stale-handle rejection and generation advancement after restart. Radio run `33797082636` passed native and ESP32 jobs.
- **2026-09-03T19:34:30Z–19:37:00Z** — Added `MeshRadioRegistry<4>` with non-recycled incarnation-scoped RadioIdentifiers plus `NeighbourDiscoveryCoordinator` binding RadioTransport SourcePeer to bounded untrusted candidates. Mesh run `33797422944` passed every host regression.
- **2026-09-03T19:38:00Z–19:41:00Z** — Added injectable `IMeshTrafficGovernor` and `DefaultMeshTrafficGovernor` with independently protected 8/4/8/8 capacities and generation-safe reservations. Regression saturates Application while proving InfrastructureResponse/ClockControl/GeneralControl remain serviceable and verifies stale reservation rejection after slot reuse. Run `33797806648` completed all build/test steps successfully; workflow cleanup remained in progress at handoff update time. No concrete PrimitiveFamilyId/EventTypeId decision was crossed.
