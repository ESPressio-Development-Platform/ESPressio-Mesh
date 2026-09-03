# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — SYSTEM / PRIMITIVE / RADIO / ESP32 / NRF24 / STATE / COMMAND RPC-REMOVAL / EVENT PROVENANCE VALIDATED; MESH CORE BOUNDED MEMBERSHIP AND DELIVERY FOUNDATIONS NOW UNDER IMPLEMENTATION.**

This file is the current absolute truth for the implementation tranche originating in `ESPressio-Mesh`. The architecture below is frozen. Implementation may choose bounded internal mechanics only where doing so does not alter public/distributed semantics, repository ownership, dependency direction, wire compatibility, locked bounds, authentication/identity semantics or Policy. If a genuine architectural omission/conflict is exposed, stop and ask Simon Stuart. Concrete ESPressio-defined `PrimitiveFamilyId` numeric allocations and the distributed EventTypeId assignment contract remain explicit decision boundaries and MUST NOT be invented autonomously.

---

# 1. Scope and branches

Implement the finalized ESPressio Mesh / Primitive structural realignment for the true 1.0.0 platform baseline. No legacy-preservation work is required unless Simon explicitly asks for it.

Primary context branch:

- `ESPressio-Mesh`: `structural_realignment_propagation`

Affected propagation branches created so far:

- `ESPressio-System`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Primitive`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Radio`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-ESP32`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-NRF24`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-State`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Command`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Event`: `structural_realignment_propagation_ESPressio-Mesh`

`ESPressio-MeshAdapters` exists architecturally but has not yet been implemented in this tranche; create/verify its matching propagation branch when adapter work begins.

Authoritative correction from Simon: the repository name is **`ESPressio-Primitive` (singular)**. Any Policy reference to `ESPressio-Primitives` is a typo.

---

# 2. Mandatory Policy rules

- Ordinary libraries MUST NOT call platform/hardware APIs directly. Portable abstractions belong in the correct ESPressio abstraction library; hardware concretes belong in platform/technology repositories.
- Existing ESPressio abstractions MUST be reused rather than reimplemented locally.
- Circular dependencies, including optional ones, MUST be avoided. If one appears necessary, stop and ask Simon before introducing it.
- Cross-library propagation remains isolated to the branches above. Every participating dependency reference in manifests, workflows/tests, examples, docs and source must target the matching propagation branch.
- Public-facing discrete components should receive useful Arduino IDE and PlatformIO demos where appropriate.
- Added/changed APIs require meaningful IDE-compatible documentation comments plus documentation revalidation.
- If GitHub Actions starts failing almost immediately because the organization task/job budget is exhausted, record that explicitly and cease treating CI as correctness evidence until the budget resets; switch to recursive self-review. **No budget-exhaustion symptom has occurred so far.**
- Internal diagnostics should use ESPressio-Logging and remain compile-switchable; inspect the current Logging API before adding instrumentation.
- Optional callback surfaces should use ESPressio-Observable multi-observer semantics. Optional Observable→Event bridges live with their source subsystem.
- This `TRANCHE_HANDOFF.MD` remains on the Mesh operative branch while the tranche is incomplete. Update it after meaningful branch/dependency/code/validation/decision/correction changes. At full completion offer the final handoff to Simon for download, then delete it from Git.
- Do not alter version numbering merely because of trench work.

---

# 3. Frozen architecture

## 3.1 Layering and ownership

- `ESPressio-System`: foundational platform-neutral identities/types/services; owns canonical `DeviceIdentifier`.
- `ESPressio-Primitive`: common conceptual-message vocabulary only (`ConceptualMessageId`, `CorrelationId`, `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, `ContractFingerprint`, minimal helpers/contracts). No giant primitive base class.
- `ESPressio-Command`: asynchronous intent semantics.
- `ESPressio-Event`: occurrence semantics.
- `ESPressio-State`: authoritative fact/replication semantics.
- `ESPressio-Radio`: opaque direct-link physical/link transport and link-local mechanics.
- `ESPressio-Mesh`: membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end Mesh delivery. Mesh must not depend on Command/Event/State.
- `ESPressio-MeshAdapters`: optional Mesh↔Command/Event/State integration above both sides.
- `ESPressio-Timing`: clock mathematics/discipline/quality. Mesh owns global root/parent coordination; Radio owns direct-link timestamp mechanics.

## 3.2 Identity and fixed semantic representations

- `DeviceIdentifier`: exact 16 B, permanent transport-independent identity, all-zero invalid. System-owned. ESP32 derives a stable default from platform/factory identity through a platform-specific namespaced transformation; application override supported.
- `MeshIdentifier`: exact nonzero 16 B opaque application-supplied Mesh identity.
- `MembershipIncarnation`: exact nonzero 16 B participation-instance identity generated from a cryptographically strong random source. Partition/radio restart does not inherently change it; genuine restart normally does. FastRejoin may preserve it only with authenticated continuity evidence.
- `MeshNodeAlias`: uint16 Mesh-local routing handle only.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; node-local and unrecycled within one incarnation.
- `MeshMessageId`: uint64, zero invalid, monotonic and non-wrapping, scoped semantically by authenticated Source DeviceIdentifier + MembershipIncarnation. Fresh independent Node delivery gets a fresh ID; retries/routes/fragments retain it; each selective-multicast recipient gets its own; Broadcast retains one through its flood.
- `ProfileGeneration`, `TopologyGeneration`: uint64 nonzero and incarnation-scoped; first generation 1; never wrap.
- Default `CapabilityMask`: uint64.
- `CanonicalName`: mandatory Mesh-unique human-readable profile property, 1–32 printable ASCII bytes, no NUL/control, no leading/trailing spaces, exact case-sensitive comparison; semantic storage 1-byte length + 32-byte backing.
- `ContractFingerprint`: exact 32 B SHA-256 semantic compatibility digest, never authentication.

## 3.3 Primitive vocabulary

`PrimitiveFamilyId` is exact uint16:

- `0x0000`: Invalid/Unspecified
- `0x0001–0x7FFF`: ESPressio-defined, individually allocated in the central Primitive registry
- `0x8000–0xFFFE`: application/private
- `0xFFFF`: reserved

Family protocol compatibility uses bounded supported version ranges and chooses the highest mutual version unless that family specifies another explicit compatible-selection rule. Family-level `ContractFingerprint` covers family semantics, not every application payload/schema. Supports / Compatible / Reachable / Authorized remain distinct concepts.

**Concrete ESPressio family numeric IDs remain UNALLOCATED.**

## 3.4 Command

Command is asynchronous intent, not RPC. Core conceptual data is identity/version/correlation/payload. Command has no intrinsic waiting, acceptance response, operation completion, result, timeout, destination/route or reply semantics. `ConceptualMessageId` identifies one Command occurrence; optional `CorrelationId` associates later independent conceptual messages. Local registry disposition says only what the local framework/handler did with the invocation, never whether the requested real-world operation completed.

## 3.5 Event

Event is an occurrence; transport is travel; local dispatch is processing. Event has no transport address or HopCount. Local/Remote origin belongs to `EventDispatchContext`, not Event object state. EventPriority is local/application dispatch policy, not Mesh QoS. Event is one-way and has no intrinsic ACK/response/completion/RPC contract. EventDispatchMethod is local execution choice. A remotely received Event is not automatically re-forwarded.

Frozen intrinsic metadata is EventTypeId, SchemaVersion, conceptual MessageId, EventPriority, payload and optional CorrelationId. The stable distributed EventTypeId assignment mechanism is not yet frozen; do not silently equate the existing stable-name FNV transport TypeID with that contract.

## 3.6 State

- `StateAddress = DeviceIdentifier + StateTypeId`; no extra SourceIdentifier.
- Local State remains application-owned and is bound/indexed/observed without a second canonical value copy. Remote replicas are State-owned.
- State is read-only outside its authority; Command represents mutation intent.
- Availability is distinct from value: Available/Stale/Unavailable/Expired with typed reasons such as SourceUnbound and locally derived SourceUnreachable.
- Typed non-owning local authority; duplicate authority rejected; no RTTI/void* registry design.
- `NotifyChanged` controls revision/observation/propagation. Explicit NotifyChanged advances revision even if equality says equivalent.
- Unbind Discard removes lineage; Retain preserves inactive metadata/revision lineage but never dangling references.
- Observer lifecycle/value/availability distinctions; synchronous deterministic bounded non-reentrant per-State notification semantics.
- Wire/control concepts: Publication, Availability, Subscribe, Unsubscribe, Resynchronize plus reciprocal subscription-result concepts where needed. No baseline ReplicaAcknowledgement; generic Disconnect removed.
- StateEpoch independent from MembershipIncarnation.
- StateObserverEventBridge belongs in State as optional Event integration.
- MeshAdapters must verify StateAddress.DeviceIdentifier against authenticated Mesh source.

## 3.7 Mesh primitive receivers / NodeProfile support

Mesh owns its own first-class control Primitive family. External families are opaque to Mesh and terminate in one active registered receiver per PrimitiveFamilyId. Unsupported family at destination is a local disposition; successful Mesh delivery remains successful if the envelope reached the destination. No receive disposition automatically causes Mesh NACK or semantic response.

Receivers receive MeshReceiveContext, selected PrimitiveProtocolVersion and bounded opaque payload. Heavy work must be delegated to the owning subsystem rather than block the Mesh receive path. Receiver registration must be bounded, deterministic and lifetime-safe.

Authenticated NodeProfile includes DeviceIdentifier, CanonicalName, CapabilityMask, bounded Groups, primitive support descriptors and ProfileGeneration. Each primitive support descriptor carries family ID, supported version range, family ContractFingerprint and exposure state. Primitive support is not Mesh-membership compatibility by default.

## 3.8 Membership, security and liveness

Admission pipeline: Unknown → Discovered → Authenticating → Validating → Joining → Active. Cheap Mesh/protocol/signature checks precede expensive cryptography. Any Active member may sponsor; sponsor is not a leader. MembershipState and ReachabilityState are distinct. Authenticated Leave is authoritative; reachability expiry is local evidence only. DeviceIdentifier is not authentication.

Reachability degrades through **Reachable → Suspect → Unreachable** using an injected liveness policy. Approximate baseline floors are 5 seconds before Suspect and 15 seconds before Unreachable but are not universal constants. Any valid authenticated Mesh evidence may immediately restore Reachable. Probes are generated only when passive/control evidence is insufficient.

Security/admission remains behind injectable abstractions such as IMeshSecurityAuthority / IMeshAdmissionPolicy. Direct-neighbour authentication and end-to-end origin authentication are distinct. Old-incarnation traffic must never mutate current-incarnation authority/dedup state.

## 3.9 Tombstones

Default `MaxMembershipTombstones = 64`, independent of `MaxMeshNodes = 32`. Tombstones contain only DeviceIdentifier, MembershipIncarnation, local monotonic retention deadline and compact disposition: LocallyForgotten / SupersededIncarnation / AuthoritativeLeave. No profile/topology/routes/radios/payload/dedup state.

Default retention ≈5 minutes. Insert removes expired entries, updates exact existing entry, uses free capacity, otherwise deterministically evicts: oldest LocallyForgotten first, then SupersededIncarnation, then AuthoritativeLeave; ties by deadline then stable identity ordering. Nothing is unevictable. Early eviction only loses local continuity optimization and never weakens authentication/admission.

## 3.10 Destinations and delivery

Public destinations: Node, Group, CapabilitySelector, Broadcast. Group/CapabilitySelector resolve once at sender into a bounded frozen recipient set then normalize to independent Node deliveries. Selective multicast shares immutable payload backing and one sender-local aggregate/deadline/handle but each recipient has independent MeshMessageId/outcome/receipt. No wire MulticastOperationId.

Ordinary wire destination is Node or Broadcast. Broadcast is best-effort controlled flood, unacknowledged, no frozen recipient set and no complete-to-all promise; one MeshMessageId persists through flood with bounded lifetime/backpressure/dedup. Reliable all-recipient behavior is selective multicast, not Broadcast.

Delivery ACK confirms only destination Mesh delivery, never semantic execution or operation completion.

## 3.11 Deduplication

Per authenticated source DeviceIdentifier + MembershipIncarnation retain `HighestObservedSequence` plus a compact sliding bitmap. Default 128 positions, minimum configurable 32, larger compile-time windows permitted. Advancing the high-water mark does not imply skipped lower sequences were observed. Unseen in-window lower sequence may be accepted; already-set is Duplicate; sequence below retained floor is TooOld and never delivered upward.

Dedup happens only after sufficient source authentication and incarnation validation. An unseen delivery first reserves bounded **InProgress** state before semantic receiver invocation so concurrent copies arriving by different Radios/routes cannot both dispatch upward. Definitive dispositions commit deduplication; TemporarilyUnavailable/ResourceUnavailable releases the reservation without committing. Deduplication is never silently disabled because of resource pressure.

Duplicate unicast/selective-multicast deliveries may regenerate required delivery acknowledgement without upper-layer redelivery. Broadcast duplicates are suppressed without ACK and need not be re-forwarded.

## 3.12 Topology/routing/retries

Mesh owns orchestration; Radio executes selected direct next hop. Injected IRoutingStrategy, IRouteAttemptPolicy, IRetryPolicy. Directed link-state; default MaxTopologyLinks 96. TopologyGeneration versions each member's complete semantic outbound-link set and advances only for material published changes. Route cache 32, max route hops 16, default hop limit 16. Forwarding is adaptive hop-by-hop; RemainingHopLimit decrements only on a successful Mesh forwarding transition.

Radio retry remains Radio-owned. Default Mesh safety ceilings: same-route attempts 3, distinct routes attempted 4. Attempts occur only if compatible with remaining immutable absolute transmission deadline.

## 3.13 Radio contract / ESP32 PHY

Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer/handle. `MaximumLogicalTransferSize` is finite and distinct from physical MTU. Admission is immediate/explicit; completion asynchronous with technology-independent direct-link outcome/evidence. Receive supplies complete opaque logical transfer plus Radio-owned source addressing/peer information, bounded observations and qualified optional timing metadata. Radio never interprets DeviceIdentifier, MembershipIncarnation, MeshMessageId, PrimitiveFamilyId or Mesh routing/destination semantics. Concrete Radio owns physical fragmentation/reassembly/retries and bounded pools. Precision timing exchange uses a separate protected path.

On ESP32, ordinary Wi-Fi and Raw80211 share one integrated PHY and one ESP32-owned coordinator. Raw follows active shared Wi-Fi channel by default. Incompatible active service yields Busy/restricted/unavailable rather than disruptive retuning. Shared channel, tx-power and power-save state are platform-owned; Mesh has no ESP32 channel semantics.

## 3.14 Clock synchronization

No permanent configured Mesh clock roles. Every eligible node is elective. Timing owns synchronization mathematics/discipline/quality, Radio direct timestamp mechanics/uncertainty, Mesh root election/parent coordination. Policies include IClockQualityPolicy, IClockEligibilityPolicy, IClockRootElectionPolicy, IClockParentSelectionPolicy. Election is quality-first with DeviceIdentifier final deterministic tie-break. Precision T1/T2/T3/T4 exchange bypasses ordinary Mesh queues/fragmentation. Clock advertisements are informational and loop prevention requires same-root ancestry with strictly improving stratum.

## 3.15 Traffic governance / execution / payload

Injected IMeshTrafficGovernor. Default protected capacities: Infrastructure Responses 8, Clock Control 4, General Control 8, Application 8. Application saturation cannot consume survival/control reserve; mandatory delivery ACKs remain serviceable. Finite control lifetimes are policy-owned.

Mesh decomposes into narrow services rather than a giant worker. Responsibility does not imply one task per service. Prefer ESPressio Task/PrecisionThread; no direct FreeRTOS in Mesh. Baseline may use one serialized Mesh execution domain. Every queue/pool is finite with explicit backpressure.

Payload forms: Borrowed Stable Payload, Bounded Owned Payload, Repeatable Serialized Source. Finite length known before acceptance. One delivery's bytes remain immutable across retries/routes/fragments. Selective multicast may share immutable backing. Adapters own family serialization, Mesh envelope/end-to-end delivery, Radio link packetization.

## 3.16 Persistence / signatures

Persistence is separately injected/composed. Policies/configuration that can alter distributed authoritative truth contribute recursively to MeshSignature via stable PolicyIdentifier, ContractVersion, ConfigurationSignature and DelegateContract; never RTTI/compiler/layout/function-address identity. Operational local mechanisms normally do not contribute.

## 3.17 Locked default capacities and planning model

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
- Infrastructure / Clock / General / Application capacities 8 / 4 / 8 / 8
- Unreachable full-member retention 60 s
- Tombstone retention ≈5 min

Provisional fully occupied fixed/cardinality metadata ≈60,160 B plus starting task-stack reservation ≈10,000 B = ≈70,160 B before variable payload/control/reassembly/runtime overhead. Ordinary payload ownership planning ≈`20 * Papp`.

`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`

Concrete logical/control/reassembly byte capacities must remain finite and measured. No-PSRAM ExternalPreferred falls back to internal DRAM.

**Architecture audit: PASS / IMPLEMENTATION READY.**

---

# 4. Current implementation state

## System

Validated. Canonical `ESPressio::System::DeviceIdentifier`: exact 16 B, zero invalid, immutable bytes, equality and deterministic ordering. Platform MAC derivation remains outside System.

## Primitive

Validated. Owns dependency-neutral family/version/message/correlation/fingerprint vocabulary and central family namespace boundaries. Concrete family IDs remain deliberately unallocated.

## Radio

Validated. Direct-link logical transfer only; finite fragmentation/reassembly/recent-transfer suppression; no Mesh routing/family semantics. Generic logical-transfer ceiling 4096 B, four reassembly slots, 32 recent-completion records. Run `33763062738` passed.

## ESP32

Validated. WiFiPhyCoordinator owns integrated-PHY arbitration; Raw80211 follows effective channel unless PHY unconstrained, reports Busy when incompatible, and tx-power/power-save writes pass through coordinator. Run `33764387563` passed.

## NRF24

Validated. 4096-B logical-transfer ceiling over 32-B physical payload; dedicated direct-clock exchange preserved. Run `33764610171` passed.

## State

Structurally complete and validated: canonical System identity; exact StateAddress; latest-fact protocol; no ReplicaAcknowledgement/Disconnect; availability/reachability separation; bounded remote/local observer/state structures; typed non-owning local authority; Retain/Discard lineage; explicit NotifyChanged; finite non-reentrant notification lanes. Runs `33778512159` and `33778720013` passed.

## Command

Intrinsic RPC/request-response/timeout/reply machinery removed. `CommandMessage` uses Primitive ConceptualMessageId, CorrelationId and PrimitiveProtocolVersion with bounded 256-B default payload and no destination/reply/timeout/result wire semantics. Run `33780014015` passed host + ESP32 JSON/Event integration.

Remaining cleanup: local registry type `CommandResult` and some comments still use result/completion wording even though it is only synchronous local handler disposition. Reconcile naming/wording without reintroducing RPC semantics.

## Event

**Provenance realignment validated.** Event objects no longer retain transport context; EventReceiver queues retain compact EventDispatchContext beside references; dispatcher/manager/thread/listener/observer paths propagate it explicitly. Remote inbound dispatches carry EventOrigin::Remote and are not automatically retransmitted. EVTT is v2 and old HopCount field is reserved. Event examples and wiki callback signatures have been reconciled to explicit context. Final cleanup run `33785167779` passed dependency-boundaries, host tests and RTTI-free ESP32 compile.

Remaining Event semantic work requires full intrinsic metadata integration. Do not choose stable distributed EventTypeId derivation or concrete PrimitiveFamilyId without Simon; existing stable-name transport TypeID is not yet proven to be the frozen EventTypeId contract.

## Mesh core

Implementation has now progressed beyond initial types/limits:

- Corrected `ReachabilityState` to frozen `Unknown / Reachable / Suspect / Unreachable` semantics.
- Added fixed-capacity `MembershipTombstoneTable` with default capacity 64, monotonic finite retention, exact refresh and frozen deterministic eviction preference. It retains only compact identity/incarnation/deadline/disposition.
- Added 24-byte default committed `DeduplicationWindow<128>` implementing HighestObservedSequence + 128-bit bitmap; unseen gaps remain admissible, duplicates are suppressed and older-than-window sequences are TooOld.
- Added non-wrapping `MeshMessageIdGenerator` issuing source/incarnation-scoped sequence values beginning at 1 and refusing issuance after uint64 maximum. It permits explicit high-water restoration only when the owning membership/security context has already authenticated continuity of the same incarnation.
- Added fixed-capacity `InboundDeliveryReservationTable` (default MaxActiveInboundDeliveries=8) keyed by authenticated Source DeviceIdentifier + MembershipIncarnation + MeshMessageId. It prevents concurrent copies of one unseen delivery from both reaching semantic handoff; it intentionally owns only InProgress reservation, leaving committed history to DeduplicationWindow.
- Umbrella header exports these foundations.
- CI checks out matching System propagation branch and compiles/runs dedicated regressions with C++17 `-Wall -Wextra -Werror`.
- Mesh run `33785734451` passed types, tombstones and dedup regression. Mesh run `33786121103` passed types, tombstones, dedup and message-ID generation. Inbound-delivery reservation validation is the current newest run after this handoff update.

---

# 5. Immediate next work

1. Confirm latest Mesh Actions run for inbound delivery reservation coverage and fix any regression.
2. Continue Mesh delivery/membership foundations that do not require unresolved PrimitiveFamilyId/EventTypeId decisions; next candidates are bounded authenticated member/incarnation records and the integration point joining auth → committed dedup classification → InProgress reservation → semantic handoff → commit/release.
3. Reconcile Command local-disposition naming/wording.
4. Propagate Primitive common vocabulary through any remaining State family-facing surfaces where required.
5. Keep Event intrinsic distributed identity work paused at the explicit EventTypeId/family-allocation decision boundary.
6. Implement higher Mesh services incrementally (membership/profile/topology/routing/governance/clock orchestration) while preserving the locked bounds and single-owner serialized-state model.
7. Create the MeshAdapters propagation branch only when adapter implementation begins.

---

# 6. Chronological UTC work log

- **2026-09-03T12:59:05Z** — Simon authorized implementation. Created Mesh primary propagation branch and initial System/Primitive/Radio propagation branches; created authoritative handoff.
- **2026-09-03T13:13:44Z** — Implemented/validated canonical System DeviceIdentifier and initial Primitive vocabulary/registry boundaries. No concrete family IDs allocated.
- **2026-09-03T13:22:31Z** — Created ESP32/NRF24/State propagation branches; repinned participating dependencies; added initial Mesh 1.0 scaffolding/types/locked limits.
- **2026-09-03T13:47:30Z** — Realigned Radio to direct-link logical transfer with bounded fragmentation/reassembly; 4096-B generic logical ceiling, four reassembly slots, 32 recent-completion records.
- **2026-09-03T13:57:30Z** — Radio CI `33763062738` passed. Added ESP32 shared-PHY coordinator and Raw80211 coexistence behavior.
- **2026-09-03T14:01:02Z** — Corrected stale ESP32 ProviderSmoke API after initial CI; run `33764387563` passed.
- **2026-09-03T14:03:32Z** — NRF24 run `33764610171` passed with 4096-B logical transfer ceiling and direct clock preserved.
- **2026-09-03T14:09:00Z–14:38:00Z** — Began State migration: canonical identity, StateAddress, latest-fact protocol, availability/reachability separation, LocalStateRegistry and bounded lifecycle semantics. Intermediate CI regressions reconciled.
- **2026-09-03T14:45:00Z–15:19:17Z** — Unified StatePublisher with local authority, bounded observer registration, non-reentrant serialized local operation lane and lifecycle/publication regressions. Runs `33769640402`, `33770242447`, `33771925619` passed.
- **2026-09-03T16:24:00Z–16:27:18Z** — Reworked RemoteStateManager bounded notification lane and completed State recursive audit. Runs `33778512159` and `33778720013` passed.
- **2026-09-03T16:40:00Z** — Created/verified Command and Event Mesh propagation branches and repinned participating dependencies.
- **2026-09-03T16:40:00Z–16:43:07Z** — Removed intrinsic Command RPC/request-response/timeout/reply machinery; added Primitive-backed bounded CommandMessage; run `33780014015` passed.
- **2026-09-03T16:45:00Z–17:12:22Z** — Added Primitive-backed Event vocabulary, removed Event-owned transport context/HopCount, moved provenance beside queued references, propagated explicit context through manager/dispatcher/thread/listener/observer paths, advanced EVTT to v2 and reconciled README/loopback example.
- **2026-09-03T17:14:00Z** — Reconciled handoff after Event provenance implementation; Event final ESP32 validation was still running at that instant.
- **2026-09-03T17:32:26Z** — Reconciled remaining PrecisionEventThread/SystemClock/ThreadInfrastructure examples plus Getting Started and Dispatching/Listening wiki material to explicit EventDispatchContext. Event run `33785167779` subsequently passed dependency, host and ESP32 jobs.
- **2026-09-03T17:36:00Z–17:38:18Z** — Verified frozen architecture source for Reachable→Suspect→Unreachable and dedup semantics. Corrected initial Mesh `Degraded` reachability label to `Suspect`; implemented bounded MembershipTombstoneTable and committed 128-position DeduplicationWindow. Mesh run `33785734451` passed all regressions.
- **2026-09-03T17:42:18Z** — Added and validated non-wrapping MeshMessageIdGenerator; Mesh run `33786121103` passed types/tombstones/dedup/message-ID tests.
- **2026-09-03T17:44:00Z** — Added fixed-capacity authenticated InboundDeliveryReservationTable to represent Decision-41 InProgress exclusion independently from committed dedup history; added regression and CI coverage. Latest validation run pending at this exact handoff write.
