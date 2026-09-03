# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — SYSTEM / PRIMITIVE / RADIO / ESP32 / NRF24 / STATE VALIDATED; COMMAND COMMON-VOCABULARY/RPC REMOVAL VALIDATED; EVENT PROVENANCE REALIGNMENT IMPLEMENTED AND UNDER FINAL VALIDATION.**

This file is the current absolute truth for the implementation tranche originating in `ESPressio-Mesh`. It is deliberately self-contained so a later session can resume without reconstructing architectural intent. The specification below is frozen. Implementation may choose bounded internal mechanics only when they do not change public/distributed semantics, repository ownership, dependency direction, wire compatibility, locked bounds, authentication/identity semantics or Policy. If implementation exposes a genuine architectural omission/conflict or requires such a change, stop and ask Simon Stuart. Concrete ESPressio-defined `PrimitiveFamilyId` numeric allocations remain an explicit decision boundary and MUST NOT be invented autonomously.

---

# 1. Scope and Branching

Implement the finalized ESPressio Mesh / Primitive structural realignment across the platform for the true 1.0.0 baseline. No legacy-preservation work is required unless Simon explicitly requests it.

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

`ESPressio-MeshAdapters` has not yet been implemented in this tranche; verify/create its matching propagation branch when adapter work begins.

Authoritative correction from Simon: the repository is **`ESPressio-Primitive` (singular)**. Any Policy spelling `ESPressio-Primitives` is a typo.

---

# 2. Mandatory Policy Rules

- Ordinary libraries MUST NOT call platform/hardware APIs directly. Platform-independent abstractions belong in the appropriate ESPressio abstraction library; concretes belong in platform/technology repositories such as ESPressio-ESP32 or ESPressio-NRF24.
- Existing ESPressio abstractions MUST be reused rather than reimplemented locally. Security uses ESPressio-Security, serialization uses ESPressio-Serializable, memory/platform mechanisms use ESPressio-System, etc.
- Circular dependencies, including optional circular dependencies, MUST be avoided. If one appears necessary, stop before introducing it and ask Simon.
- Cross-library propagation work remains isolated to the propagation branches above. Every reference to a participating repository — manifests, workflows/tests, examples, docs or source-level dependency references — must point to its matching propagation branch.
- Public-facing discrete components should receive useful demos where appropriate, in both `demos/<name>/arduino_ide` and `demos/<name>/platformio`, compilable, decomposed and usefully commented.
- Added/changed APIs require meaningful IDE-compatible documentation comments and revalidation of related comments/README material.
- If GitHub Actions begins failing within seconds because the organization task/job budget is exhausted, explicitly record that fact; until the next month CI is not correctness evidence and work must use recursive self-review instead. No such exhaustion has occurred in this tranche so far.
- Internal diagnostics should use ESPressio-Logging and be compile-switchable; inspect the current Logging API before instrumenting.
- Optional callback surfaces should use ESPressio-Observable multi-observer semantics rather than one-off callback members. Observable→Event bridges remain optional and live in the source subsystem.
- This `TRANCHE_HANDOFF.MD` remains on the Mesh operative branch while the tranche is incomplete. Update it after meaningful branch/dependency/code/validation/decision/correction changes. At full completion offer the final handoff to Simon for download first, then delete it from Git.
- Current Working Branch baseline for platform repositories is `structural_realignment` (Labs uses `main`); propagation branches derive from those Working Branches.
- Do not alter version numbering merely as part of trench work. Documentation for the eventual release baseline should describe 1.0.0 semantics rather than historical releases.

---

# 3. Frozen Architecture

## 3.1 Layering

- `ESPressio-System`: foundational platform-neutral identities/types/services; owns canonical `DeviceIdentifier`.
- `ESPressio-Primitive`: only common conceptual-message vocabulary (`ConceptualMessageId`, `CorrelationId`, `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, `ContractFingerprint`, minimal helpers). It must not become a giant common base-class library.
- `ESPressio-Command`: asynchronous intent semantics.
- `ESPressio-Event`: occurrence semantics.
- `ESPressio-State`: authoritative fact/replication semantics.
- `ESPressio-Radio`: opaque direct-link physical/link transport and link-local mechanics.
- `ESPressio-Mesh`: membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end delivery over Radio. Mesh must not depend on Command/Event/State.
- `ESPressio-MeshAdapters`: optional Mesh↔Command/Event/State integration above both sides.
- `ESPressio-Timing`: clock mathematics/discipline/quality. Mesh owns distributed root election/parent coordination; Radio owns direct-link timestamp mechanics.

## 3.2 Identity and fixed semantic sizes

- `DeviceIdentifier`: exactly 16 B, permanent transport-independent identity, System-owned; all-zero invalid. ESP32 default derives stably from factory identity/MAC through platform-specific namespaced transformation with application override.
- `MeshIdentifier`: exactly 16 B, application supplied, nonzero.
- `MembershipIncarnation`: exactly 16 B, nonzero participation instance; partition/radio restart does not inherently change it.
- `MeshNodeAlias`: uint16 Mesh-local routing handle only.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved.
- `MeshMessageId`: uint64 nonzero monotonic scoped to authenticated source DeviceIdentifier+MembershipIncarnation; one independently routable Node delivery receives one ID; each selective-multicast recipient its own; Broadcast keeps one ID across its flood.
- `ProfileGeneration`, `TopologyGeneration`: uint64 nonzero.
- default `CapabilityMask`: 8 B.
- `CanonicalName`: mandatory Mesh-unique NodeProfile property, 1–32 printable ASCII bytes, no NUL/control, no leading/trailing spaces, exact case-sensitive comparison; semantic backing 33 B before containing alignment.
- `ContractFingerprint`: exactly 32 B SHA-256 semantic compatibility digest, not authentication.

## 3.3 Primitive family vocabulary

`PrimitiveFamilyId` is exact uint16:

- `0x0000`: Invalid/Unspecified
- `0x0001–0x7FFF`: ESPressio-defined, individually allocated in one central registry in ESPressio-Primitive
- `0x8000–0xFFFE`: application/private
- `0xFFFF`: reserved

Family ID identifies conceptual family. `PrimitiveProtocolVersion` expresses family protocol revision. Version ranges negotiate highest mutual version. `ContractFingerprint` detects semantic-contract disagreement. Supports / Compatible / Reachable / Authorized are distinct concepts.

**Concrete ESPressio family numeric IDs remain UNALLOCATED. Do not choose values without Simon's explicit decision.**

## 3.4 Command semantics

- Command = asynchronous intent, not RPC.
- Core conceptual data is message identity/version/correlation/payload.
- No intrinsic wait, acceptance, operation completion, result, response expectation, timeout, route/address or reply semantics.
- `ConceptualMessageId` identifies one Command occurrence; optional `CorrelationId` associates later independent messages.
- Outcomes are separate reciprocal Command/Event/State concepts where appropriate.
- Local registry/dispatch success means only local framework acceptance/handling disposition, not completion of the requested operation.

## 3.5 Event semantics

- Event = occurrence; transport = travel; local dispatch = processing. These remain separate.
- Intrinsic Event data is `EventTypeId`, `SchemaVersion`, conceptual `MessageId`, `EventPriority`, payload and optional `CorrelationId`.
- Event contains no transport address or HopCount.
- Local/Remote origin is receive/dispatch context, not Event object state.
- EventPriority is an application/local-dispatch hint, not Mesh QoS.
- Event is one-way with no intrinsic acknowledgement, response, completion or RPC contract.
- EventDispatchMethod is a local execution choice.
- A remotely received Event is not automatically forwarded onward.

## 3.6 State semantics

- `StateAddress = DeviceIdentifier + StateTypeId`; no separate SourceIdentifier.
- Local State remains application-owned; State binds/indexes/observes it without a second canonical value copy. Remote replicas are State-owned.
- State is read-only outside its authority; Command represents mutation intent.
- Availability is separate from value; includes Available/Stale/Unavailable/Expired and reasons including SourceUnbound/SourceUnreachable, with remote effective availability combining authoritative source status and Mesh reachability.
- Typed non-owning binding preferred; avoid RTTI/void* indirection. Duplicate local authority rejected.
- `NotifyChanged` controls revision/observation/remote propagation. Explicit NotifyChanged advances revision even where equality considers a value equivalent. Reference-backed State does not keep a hidden value shadow solely for equality.
- Unbind `Discard` removes lineage; `Retain` preserves inactive metadata/revision lineage but never dangling source references.
- Observers have distinct value/availability/lifecycle semantics; notifications synchronous/deterministic by default, bounded, and same-State non-reentrant/serialized.
- State protocol: Publication, Availability, Subscribe, Unsubscribe, Resynchronize (and reciprocal subscription-result concepts where implemented). No baseline ReplicaAcknowledgement; generic Disconnect removed.
- `StateEpoch` independent from MembershipIncarnation; revisions monotonic within epoch. Retain preserves lineage, Discard creates new epoch.
- State publications may coalesce because State is latest fact; every transition belongs in Event/stream semantics instead.
- `StateObserverEventBridge` lives in State as optional Event integration.
- MeshAdapters validates `StateAddress.DeviceIdentifier` against authenticated Mesh source.

## 3.7 Mesh primitive/adapters and support advertisement

- Mesh has a first-class Mesh Primitive family for infrastructure/control traffic; it is not Command/Event/State.
- Common Mesh envelope includes extensible `PrimitiveFamilyId` plus opaque family payload.
- Mesh core understands only its own family; external families dispatch to one active registered receiver per family.
- Unknown family at destination still means Mesh transport delivery succeeded if envelope arrived; unsupported-family is local disposition.
- MeshAdapters provides independently consumable CommandMeshAdapter/EventMeshAdapter/StateMeshAdapter/future adapters.
- `PrimitiveReceiveContext` contains only transport-independent provenance; Mesh-specific alias/incarnation/delivery information lives separately in `MeshReceiveContext`.
- NodeProfile advertises bounded family support descriptors: family ID, supported version range, fingerprint, exposure. Family support usually does not affect Mesh membership compatibility/signature unless explicitly elevated.

## 3.8 Mesh membership/security

- One active Mesh per device for 1.0.0.
- Admission pipeline: Unknown → Discovered → Authenticating → Validating → Joining → Active.
- Cheap protocol/MeshIdentifier/MeshSignature checks precede crypto.
- Any Active member may sponsor a compatible/authenticated candidate; sponsor is not leader.
- Bootstrap first member only after configured interval with no compatible mesh observed.
- MembershipState distinct ReachabilityState.
- Authenticated Leave is authoritative for represented participation; local reachability expiry is not global termination.
- FastRejoin/continuity evidence never replaces authentication.
- Security/admission behind injected abstractions such as `IMeshSecurityAuthority` / `IMeshAdmissionPolicy`. DeviceIdentifier is not proof of authentication.

## 3.9 Membership tombstones

- Separate bounded table, default 64, independent of active-member capacity 32.
- Scope: DeviceIdentifier + MembershipIncarnation.
- Stores only identity, incarnation, local monotonic retention deadline and compact disposition: at least LocallyForgotten, SupersededIncarnation, AuthoritativeLeave.
- No profile/topology/routes/radios/payload/dedup data.
- Default retention ≈5 min.
- Insert: expire old; update exact; use free slot; otherwise deterministic eviction.
- Eviction order: oldest LocallyForgotten, then SupersededIncarnation, then AuthoritativeLeave; ties by deadline then stable identity ordering.
- Nothing is unevictable. Early eviction only loses local continuity optimization; later claimant receives no privilege and must fully authenticate/admit. Tombstone exhaustion cannot weaken authentication or consume active-member capacity.

## 3.10 Destinations and delivery

- Public destinations: Node, Group, CapabilitySelector, Broadcast.
- Group/CapabilitySelector resolve once at sender to frozen bounded recipient set, then N independent Node deliveries.
- Selective multicast has one sender-local aggregate, frozen recipients, one immutable shared payload, one aggregate deadline/handle, independent per-recipient MessageId/outcome/receipt; no wire MulticastOperationId.
- Ordinary wire destination only Node or Broadcast.
- Broadcast = best-effort controlled flood, no frozen recipient set, no destination ACK, no complete-to-all promise, one MessageId throughout flood, bounded lifetime/backpressure and duplicate suppression. No reliable Broadcast mode.
- Delivery ACK confirms delivery only, never operation completion.

## 3.11 Topology/routing/retries

- Mesh owns end-to-end orchestration; Radio executes selected next hop.
- Injectable `IRoutingStrategy`, `IRouteAttemptPolicy`, `IRetryPolicy`.
- Directed bounded link-state baseline; distinct edges per Radio where relevant.
- Origin-authenticated topology advertisements relayed unchanged.
- `TopologyGeneration` represents semantic complete outbound-link set; freshness separate.
- Route cache local/disposable.
- Adaptive hop-by-hop forwarding; `RemainingHopLimit` decrements only on successful Mesh forwarding transition.
- Defaults: max topology links 96, route cache 32, max route hops 16, default hop limit 16, same-route attempts 3, routes attempted 4, dedup window 128, full unreachable retention 60s.

## 3.12 Clock synchronization

- No configured permanent clock roles; every eligible node elective.
- Timing owns math/discipline/quality; Radio owns direct timestamp capabilities/uncertainty; Mesh owns global root election, parent choice, stratum/quality propagation and partition behavior.
- Hierarchical direct-link sync; data route may differ from time-parent path.
- Policies: `IClockQualityPolicy`, `IClockEligibilityPolicy`, `IClockRootElectionPolicy`, `IClockParentSelectionPolicy`.
- Root election quality-first with DeviceIdentifier deterministic tie-break.
- Precision T1/T2/T3/T4 exchange uses separately protected Radio/Timing path and bypasses ordinary Mesh routing/fragmentation/application queues where required.
- Clock advertisements informational; independent calculation; loops prevented by same-root ancestry + strictly improving stratum.

## 3.13 Traffic governance / execution

- Injected `IMeshTrafficGovernor`; protected default capacities: Infrastructure Responses 8, Clock Control 4, General Control 8, Application 8.
- Application saturation cannot consume protected control reserves. Mandatory ACKs remain serviceable. Control lifetimes finite via injected policy.
- Mesh decomposes into narrow services, not one giant worker. Semantic responsibility does not imply one RTOS task per service.
- Prefer ESPressio Task/PrecisionThread abstractions, no direct RTOS use in Mesh.
- Baseline may use one serialized Mesh execution domain with service maintenance deadlines/wake signaling.
- All queues finite/bounded with explicit backpressure.

## 3.14 Payload ownership

Supported logical payload forms: Borrowed Stable Payload, Bounded Owned Payload, Repeatable Serialized Source.

- Finite length known before acceptance.
- One Mesh delivery MessageId maps to immutable logical bytes across retries/routes/fragments.
- Selective-multicast recipients may share immutable backing but not MessageId.
- No mutable reserialization between retries.
- State may coalesce before commit; committed Mesh payload immutable.
- Adapters own family serialization; Mesh owns envelope/end-to-end delivery; Radio owns link packetization/fragmentation.

## 3.15 Radio contract

- Public Radio abstraction accepts one complete immutable bounded logical transfer to one directly reachable Radio peer/address.
- `MaximumLogicalTransferSize` finite and distinct from physical MTU.
- Mesh provides bounded attempt duration derived from immutable absolute Mesh deadline.
- Admission immediate/explicit; accepted work completes asynchronously with technology-independent direct-link outcome/evidence, distinguishing transmission completion from peer acknowledgement where relevant.
- Receive delivers complete opaque logical transfer plus Radio-owned source address, bounded observations and explicitly qualified optional timing metadata.
- Radio never interprets/constructs DeviceIdentifier, MembershipIncarnation, MeshMessageId, PrimitiveFamilyId, destination/group/routing semantics.
- Concrete Radio owns fragmentation/reassembly and physical retries; all pools bounded.
- Precision timing exchange is separate protected path.

## 3.16 ESP32 shared Wi-Fi PHY

- Shared physical resource has one platform-level owner/arbitrator.
- Ordinary Wi-Fi and Raw80211 share integrated PHY; neither independently retunes hardware-global state.
- Raw80211 follows effective shared Wi-Fi channel by default. Fixed Raw channel constrains only when PHY otherwise unconstrained.
- Incompatible active service => Raw reports Busy/restricted/unavailable rather than disrupting service.
- Reassociation/channel changes cause Radio/link availability/topology convergence, not device or membership identity changes.
- Mesh has no ESP32 channel semantics.
- ESPressio-ESP32 owns shared-PHY coordination including channel, power-save and tx-power arbitration.

## 3.17 Persistence and policy signatures

- Persistence is separate and injected/composed; Mesh does not reimplement it.
- Persisted structures preserve live ownership/boundedness contracts.
- Policies/config that alter distributed authoritative truth contribute recursively to MeshSignature via stable PolicyIdentifier, ContractVersion, ConfigurationSignature and DelegateContract. Never use RTTI/compiler/layout/function-address identity.
- Operational local mechanisms normally do not affect MeshSignature.

## 3.18 Locked default capacities and memory model

Defaults:

- MaxMeshNodes 32
- MaxRadiosPerNode 4
- MaxGroupsPerNode 8
- MaxPrimitiveReceivers 8
- MaxActiveApplicationTransmissions 8
- MaxTopologyLinks 96
- MaxRouteHops 16
- DefaultHopLimit 16
- Route cache 32
- MaxActiveInboundDeliveries 8
- MaxPendingNeighbourCandidates 8
- MaxActiveLivenessProbes 4
- MaxActiveInboundAuthentications 4
- MaxRecipientsPerTransmission 32
- MaxMembershipTombstones 64
- protected capacities 8 / 4 / 8 / 8
- same-route attempts 3
- routes attempted 4
- dedup 128
- unreachable retention 60s
- tombstone retention ≈5min

Provisional fully occupied fixed/cardinality metadata ≈60,160 B (58.75 KiB), plus starting task-stack reservation ≈10,000 B, yielding ≈70,160 B (68.52 KiB) before variable payload/control/reassembly/runtime overhead.

Ordinary payload ownership ≈`20 * Papp`. Planning formula:

`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`

All concrete transfer/reassembly/control capacities must remain finite and measured. No-PSRAM ESP32 builds assume ExternalPreferred can fall back to internal DRAM.

**Architecture audit status: PASS / IMPLEMENTATION READY.**

---

# 4. Current Implementation State

## System

Validated. Canonical `ESPressio::System::DeviceIdentifier` is exact 16 B, all-zero invalid, immutable bytes, equality and deterministic ordering. Platform-specific MAC derivation is not in System.

## Primitive

Validated. Owns dependency-neutral `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, strong uint64 `ConceptualMessageId`, strong uint64 `CorrelationId`, version-range negotiation and exact 32-B `ContractFingerprint`, plus central family namespace boundaries. Concrete family IDs intentionally remain unallocated.

## Radio

Validated. Owns only bounded direct-link logical transfer over caller-selected Radio/RadioAddress with finite fragmentation/reassembly and recent-transfer suppression. No Mesh routing/hop/family semantics remain. Generic logical-transfer ceiling 4096 B, four reassembly slots, 32 recent-completion records. Actions run `33763062738` passed.

## ESP32

Validated. `WiFiPhyCoordinator` owns integrated-PHY arbitration; ordinary Wi-Fi owns active global PHY policy, Raw80211 follows effective channel unless unconstrained, incompatible Raw operation reports Busy/unavailable, and tx-power/power-save writes pass through coordinator. Raw80211 advertises 4096-B logical transfer over ~270-B physical frame. Corrected CI run `33764387563` passed.

## NRF24

Validated. 4096-B logical-transfer ceiling over 32-B physical payload; separate atomic direct-clock exchange preserved. CI `33764610171` passed.

## State

Structurally complete and validated. Canonical System DeviceIdentifier; exact StateAddress; protocol-v2/latest-fact semantics; no ReplicaAcknowledgement or Disconnect; source availability separated from reachability; bounded remote manager/device/observer storage; typed non-owning local authority via `LocalStateRegistry`/`StatePublisher`; Retain/Discard lineage semantics; explicit NotifyChanged; finite serialized non-reentrant local and remote notification lanes. Final runs `33778512159` and `33778720013` passed all State jobs. No task-budget symptom.

## Command

Propagation branch created and participating dependencies repinned. The previous intrinsic RPC model has been removed: request/response envelopes, response expectation/mode, timeout tracking, pending requests, response routes, Command Event executor, completion/timeout Event types and their decomposition CI machinery are gone. `CommandMessage` uses Primitive `ConceptualMessageId`, `CorrelationId`, `PrimitiveProtocolVersion` and a bounded 256-B default payload representation, with no destination/reply/timeout/result semantics. Command run `33780014015` passed host tests plus ESP32 JSON and optional Event integration.

Remaining Command cleanup: local registry still calls its synchronous local handler disposition `CommandResult` and some comments use completion language. This is not a wire/RPC result, but wording/API should be reconciled so local success means only accepted local handling, consistent with frozen Command semantics.

## Event

Propagation branch created and System/Primitive dependency references repinned. `ESPressio_EventTypes.hpp` now exposes EventTypeId, EventSchemaVersion, Primitive-backed EventMessageId/EventCorrelationId/EventProtocolVersion, EventOrigin and a compact transport-independent `EventDispatchContext` containing only Local/Remote provenance.

Event objects no longer retain transport context. `IEvent` and `Event<TTime>` have no `__setDispatchContext` / `__getDispatchContext`. `EventReceiver` retains context beside each bounded queue entry, and EventDispatcher propagates it beside Event references through fan-out. EventManager observer work carries the same context. EventListener/IEventObserver callbacks now receive the dispatch context explicitly. EventThread, EventThreadWithLoop and PrecisionEventThread preserve that context through their processing path.

Event Transport has been realigned so remotely received Events are submitted to EventManager with `EventOrigin::Remote`; EventTransportManager forwards outbound only when the queued dispatch context is Local. Therefore an inbound remote Event is not automatically retransmitted. Transport-local message/hop state is no longer placed on the Event object. EVTT envelope was deliberately advanced to version 2 and its old HopCount byte is now Reserved; HopCount was removed from transaction diagnostics as well. Historical wire compatibility is intentionally not preserved for this 1.0 restructuring.

Host Event tests and dependency-boundary checks are passing on current provenance work. Latest final ESP32 propagation compile was still in progress when this handoff was reconciled; inspect the newest Event Actions run before declaring Event validation complete.

Open Event semantic work after provenance validation: the frozen intrinsic metadata still requires full integration of stable EventTypeId, SchemaVersion, conceptual MessageId, EventPriority and optional CorrelationId with concrete Event instances/wire serialization. Do not invent the distributed EventTypeId derivation/registry or a concrete PrimitiveFamilyId while doing this; if the existing stable-name transport TypeID cannot be proven to be the frozen EventTypeId contract, this is a decision boundary for Simon.

---

# 5. Immediate Next Work

1. Inspect latest Event Actions run and correct any remaining ESP32/bridge fallout from the dispatch-context callback signature.
2. Audit/update Event examples/wiki/CHANGELOG/docs for context separation and propagation branch references; compile representative demos where practical.
3. Determine how much intrinsic Event metadata can be wired without making an unfrozen distributed EventTypeId/message-ID allocation decision. Stop and ask Simon if stable EventTypeId derivation or concrete PrimitiveFamilyId allocation must be chosen.
4. Reconcile Command local-disposition naming/wording so `CommandResult` cannot imply remote/operation completion, without reintroducing RPC semantics.
5. Propagate Primitive common vocabulary through State family-facing surfaces where still required.
6. Continue to Mesh core services and later MeshAdapters only after lower family surfaces are coherent; create MeshAdapters propagation branch when adapter implementation starts.

---

# 6. Chronological UTC Work Log

- **2026-09-03T12:59:05Z** — Simon authorized implementation. Created Mesh primary propagation branch and initial System/Primitive/Radio propagation branches; created authoritative handoff.
- **2026-09-03T13:13:44Z** — Implemented/validated canonical System DeviceIdentifier and initial Primitive vocabulary/registry boundaries. No concrete family IDs allocated.
- **2026-09-03T13:22:31Z** — Created ESP32/NRF24/State propagation branches; repinned participating dependencies; added initial Mesh 1.0 scaffolding/types/locked limits.
- **2026-09-03T13:47:30Z** — Realigned Radio to direct-link logical transfer with bounded fragmentation/reassembly; 4096-B generic logical ceiling, four reassembly slots, 32 recent-completion records.
- **2026-09-03T13:57:30Z** — Radio CI `33763062738` passed. Added ESP32 shared-PHY coordinator and Raw80211 coexistence behavior.
- **2026-09-03T14:01:02Z** — Corrected stale ESP32 ProviderSmoke API after initial CI; run `33764387563` passed.
- **2026-09-03T14:03:32Z** — NRF24 run `33764610171` passed with 4096-B logical transfer ceiling and direct clock preserved.
- **2026-09-03T14:09:00Z–14:38:00Z** — Began State migration: canonical identity, StateAddress, latest-fact protocol, availability/reachability separation, LocalStateRegistry and bounded lifecycle semantics. Intermediate CI regressions were reconciled.
- **2026-09-03T14:45:00Z–15:19:17Z** — Unified StatePublisher with local authority, bounded observer registration, non-reentrant serialized local operation lane and lifecycle/publication regressions. Runs `33769640402`, `33770242447`, `33771925619` passed.
- **2026-09-03T16:24:00Z** — Reworked RemoteStateManager notifications to fixed four-batch serialized lane.
- **2026-09-03T16:25:16Z** — Remote State reentrancy regression passed in run `33778512159`.
- **2026-09-03T16:27:18Z** — State recursive self-audit/docs complete; follow-up `33778720013` passed. State marked structurally complete for tranche.
- **2026-09-03T16:40:00Z** — Created/verified Command and Event Mesh propagation branches and repinned participating dependencies.
- **2026-09-03T16:40:00Z–16:43:07Z** — Removed intrinsic Command RPC/request-response/timeout/reply machinery; added Primitive-backed bounded `CommandMessage`; Command run `33780014015` passed host + ESP32 JSON/Event integration.
- **2026-09-03T16:45:00Z** — Added initial Primitive-backed Event family vocabulary without allocating PrimitiveFamilyId.
- **2026-09-03T16:50:00Z–17:05:49Z** — Removed intrinsic Event transport context from IEvent/Event; moved Local/Remote dispatch provenance into bounded EventReceiver queue entries and propagated through EventDispatcher/EventManager. Removed HopCount semantics and advanced EVTT envelope to v2. Updated host regressions and CI dependency checkouts for Primitive propagation.
- **2026-09-03T17:09:49Z** — Extended explicit dispatch provenance through EventListener/IEventObserver and EventThread/PrecisionEventThread callback surfaces; host regression verifies Remote provenance reaches listener callbacks without living on Event objects.
- **2026-09-03T17:10:39Z** — Updated EventTransportLoopback example to consume dispatch context explicitly rather than reading transport state from Event.
- **2026-09-03T17:12:22Z** — Reconciled Event README to 1.0 occurrence semantics, Primitive vocabulary, EVTT v2 context separation and propagation-branch dependencies. Latest Event host/dependency-boundary jobs were passing; ESP32 compile still running at handoff update time. No Actions-budget exhaustion symptom.
- **2026-09-03T17:14:00Z** — Rewrote/reconciled this handoff to current absolute tranche truth, retaining frozen architecture and all meaningful implementation/validation history while marking Event final validation and intrinsic metadata integration as the active front.
