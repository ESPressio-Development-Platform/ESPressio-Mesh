# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — STATE LOCAL AUTHORITY/OBSERVATION REALIGNMENT VALIDATED; STATE SELF-AUDIT UNDERWAY.**

This file is the current absolute truth for this implementation tranche. It is intentionally self-contained so that another ChatGPT/AI session can resume the tranche without reconstructing architectural intent from prior conversations.

The specification embedded below is **FROZEN for this tranche**. An implementing agent MUST NOT reinterpret, redesign, weaken, broaden, or otherwise mutate it autonomously. If implementation exposes a conflict, omission, infeasibility, circular dependency, public-API consequence, distributed-semantic consequence, memory-bound violation, or other reason the specification may need to change, the agent MUST stop and report the issue to Simon Stuart. Only Simon's explicit decision may amend the specification. The handoff MUST be updated first to reflect any newly agreed absolute truth before implementation resumes.

The chronological work log is operational history only; entries in that log do not become architecture decisions merely because they are recorded there.

---

# 1. Scope of Work

Implement the finalized ESPressio Mesh / Primitive structural realignment architecture across the ESPressio Development Platform, beginning from the current Working Branches defined by Policy. The primary context repository is `ESPressio-Mesh`; cross-library changes are to be performed on propagation branches derived from each affected repository's current Working Branch.

The tranche includes all code, tests, documentation, dependency repinning, optional integration bridges, platform concretes, demos where appropriate, and validation required to realize the frozen architecture below. It also includes the required reconciliation of existing code with the new ownership boundaries, especially System identity, Primitive vocabulary, Radio abstraction/concretes, Mesh core, MeshAdapters, Timing integration, Security admission, Persistence composition, Logging instrumentation, Observable integration, and ESP32 shared-PHY behaviour.

No legacy-preservation work is required unless Simon explicitly requests it. This tranche targets the platform-wide true 1.0.0 baseline.

---

# 2. Authoritative Policy Rules

The following rules are mandatory. Exceptions are permitted only when Simon explicitly states the exception.

## 2.1 Abstraction Policy

- Ordinary ESPressio libraries MUST NOT directly call hardware/platform APIs or depend on hardware/platform identifiers such as Arduino or ESP-IDF types.
- Hardware-level operations, including memory allocation, MUST use and, where necessary, extend ESPressio-language abstractions owned by `ESPressio-System`.
- Concrete platform/hardware implementations belong only in appropriate concrete repositories such as `ESPressio-ESP32` or technology-specific libraries.
- Context abstractions belong in the context-owning library. Example: Radio concepts/interfaces belong in `ESPressio-Radio`; an NRF24 concrete belongs in `ESPressio-NRF24`; an ESP32 integrated-radio concrete belongs in `ESPressio-ESP32`.
- Existing ESPressio functional abstractions MUST be reused rather than reimplemented locally. Security/encryption uses `ESPressio-Security`; serialization/deserialization uses `ESPressio-Serializable`; appropriate concrete implementations should ordinarily be injected at application composition level.
- Where a protocol mandates one concrete serialization/implementation internally, that exception must be deliberate, documented, and communicated.

## 2.2 Circular Dependency Policy

- Circular dependencies between ESPressio libraries MUST be avoided, including optional circular dependencies.
- If a design/implementation choice would introduce a circular dependency, implementation MUST stop before introducing it.
- The issue, dependency path, and a proposed architectural alternative must be explained to Simon.
- A circular dependency may be accepted only after Simon gives explicit approval.

## 2.3 Cross-Library Change / Propagation Policy

- When work in a primary context repository requires reciprocal changes in other repositories, the primary context repository uses a branch suffixed `_propagation`.
- Every affected upstream/downstream repository uses a branch created from its current Working Branch and suffixed `_propagation_<OriginatingLibrary>`.
- For this tranche the origin is `ESPressio-Mesh`, therefore affected repositories use `_propagation_ESPressio-Mesh`.
- All dependency references to repositories participating in the propagation set MUST be repointed to the matching propagation branch, not just manifest references but also tests, jobs, tasks, examples, CI automation, documentation or source-level dependency references.
- Propagation work must remain isolated to those propagation branches.

## 2.4 Demo Policy

- Public-facing discrete components should receive an illustrative demo wherever that materially helps a consuming developer understand use.
- Demos MUST compile.
- Demos live under `demos/<logical-name>/arduino_ide` and `demos/<logical-name>/platformio`.
- Both Arduino IDE and PlatformIO versions are required.
- Demo code must be decomposed and engineered comparably to ESPressio source: no monolithic `setup()`/`loop()`, sensible class ownership/encapsulation, and source separation by responsibility.
- Demo code should be heavily and usefully commented so the consuming developer can follow what each relevant line is doing.

## 2.5 Documentation Policy

- Added or modified code requires meaningful comments, particularly IDE-compatible documentation comments used by Visual Studio symbol/context popups.
- A change requires revalidation of related comments beyond the directly edited lines, especially for interface-breaking changes whether public or internal.
- Every repository README must remain accurate after changes.
- README structure should remain consistent with the established ESPressio repository pattern unless Simon explicitly directs otherwise.
- README content should help a prospective consuming developer determine whether and how the library is appropriate for their application.

## 2.6 GitHub Task Result Policy

- If GitHub Actions jobs begin failing within seconds because the organization has exhausted its Task/Job budget, the tranche summary MUST explicitly state this.
- From that point until the first day of the next calendar month, CI results MUST NOT be treated as reliable evidence of code correctness/failure.
- The implementation approach must switch to recursive self-review: after each change, inspect the complete lifecycle/context affected by that change and reconcile any breakage.
- If consequences cannot be safely reconciled, stop, describe the issue and attempted considerations, and await Simon's direction.

## 2.7 Logging Instrumentation Policy

- Useful internal library diagnostics should be instrumented wherever sensible.
- Internal logging MUST be compile-switchable so unwanted debugging diagnostics compile out.
- Logging MUST use `ESPressio-Logging`, not ad-hoc platform logging.
- The implementation must inspect the current `ESPressio-Logging` Working Branch/API before instrumenting against it.

## 2.8 Observer / Callback Policy

- Where optional callback behaviour makes sense, use `ESPressio-Observable` to provide multi-observer capability rather than a single `_OnSomething` callback member.
- Where an Observable has useful decoupled Event semantics, provide an optional Observable→Event bridge where appropriate.
- Such bridges should normally be single-instance per Observable to prevent duplicate Event multiplication.
- The Event bridge and its dependency on `ESPressio-Event` MUST remain optional.

## 2.9 Tranche Timeout / Interruption Policy

- This `TRANCHE_HANDOFF.MD` must remain on the operative branch of the highest-order/context repository (`ESPressio-Mesh`) for the entire in-progress tranche.
- It must contain the full scope, policy rules, frozen specification, and a chronological UTC-timestamped status log.
- Every meaningful status change, branch operation, dependency repin, implementation change, validation result, discovered issue, decision, and correction MUST be logged proactively.
- If the tranche is interrupted before completion, continuation must be scheduled at the earliest possible opportunity allowed by the execution environment.
- When the tranche is fully complete, the final handoff must first be offered to Simon for download via the initiating chat, and only then deleted from Git. Deletion symbolizes complete tranche completion.

## 2.10 Working Branch Policy

All repositories named below use `structural_realignment` as their current Working Branch unless a propagation branch is required for this tranche. `Labs` uses `main`.

`ESPressio-System`, `ESPressio-ESP32`, `ESPressio-Task`, `ESPressio-Threads`, `ESPressio-Observable`, `ESPressio-Serializable`, `ESPressio-Units`, `ESPressio-Timing`, `ESPressio-Event`, `ESPressio-Command`, `ESPressio-State`, `ESPressio-Persistence`, `ESPressio-Security`, `ESPressio-WiFi`, `ESPressio-ESP-Now`, `ESPressio-Web`, `ESPressio-Tree`, `ESPressio-Radio`, `ESPressio-Sockets`, `ESPressio-NRF24`, `ESPressio-Serial`, `ESPressio-Display`, `ESPressio-Logging`, `ESPressio-Mesh`, `ESPressio-MeshAdapters`, `ESPressio-Primitive`, `ESPressio-Demos`.

**Authoritative correction from Simon:** the Policy file's `ESPressio-Primitives` spelling is erroneous. The repository is `ESPressio-Primitive` (singular), and its Working Branch is `structural_realignment`.

---

# 3. Frozen Final Specification — Decisions 1–50D Reconciled

This section is the normative implementation specification. It represents the final architecture after the implementation-readiness audit passed. Historical superseded proposals are intentionally omitted.

## 3.1 Architectural Layering and Ownership

- `ESPressio-System` owns foundational platform-neutral identities/types/services and the canonical `DeviceIdentifier`.
- `ESPressio-Primitive` owns only common conceptual-message vocabulary: `ConceptualMessageId`, `CorrelationId`, `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, and minimal dependency-neutral helpers/contracts. It MUST NOT become a giant common base-class library.
- `ESPressio-Command` owns asynchronous intent semantics.
- `ESPressio-Event` owns occurrence semantics.
- `ESPressio-State` owns authoritative fact/replication semantics.
- `ESPressio-Radio` owns the opaque physical/link transport abstraction and link-local mechanics.
- `ESPressio-Mesh` owns membership, topology, routing, distributed-control plane, logical addressing, admission orchestration, multi-radio forwarding, and end-to-end Mesh delivery over Radio.
- `ESPressio-MeshAdapters` owns optional Mesh↔conceptual-primitive integration adapters and sits above Mesh plus Command/Event/State. Lower layers MUST NOT depend upward on it.
- `ESPressio-Timing` is not a conceptual primitive family. Timing owns clock mathematics, discipline and quality; Mesh owns distributed root election, time-parent coordination and Mesh-owned clock-control transport.
- Public APIs expose bounded read-only views where practical; mutation stays with the subsystem that owns the state.
- Internal workers/tasks/schedulers are implementation mechanics, not semantic public API.

## 3.2 Canonical Device Identity

- `DeviceIdentifier` is permanent, transport-independent ESPressio identity owned by `ESPressio-System`.
- `DeviceIdentifier` is exactly 16 bytes.
- `MeshNodeAlias` is only a compact Mesh-local routing handle and is not identity.
- `RadioAddress` is only a technology-specific link endpoint and is not identity.
- ESP32's default DeviceIdentifier is a stable 128-bit value derived from factory device identity/Wi-Fi MAC using a namespaced transformation, with application override supported.

## 3.3 Primitive Vocabulary

- `PrimitiveFamilyId` is an exact `uint16_t` wire/API value.
- `0x0000` = Invalid/Unspecified.
- `0x0001–0x7FFF` = ESPressio-defined family IDs.
- `0x8000–0xFFFE` = application/private family IDs.
- `0xFFFF` = reserved sentinel.
- Private IDs are privately coordinated; there is no global private registry or hash/name derivation.
- ESPressio-defined IDs are individually allocated from one central registry in `ESPressio-Primitive`; repositories do not receive coarse ID blocks.
- Family ID identifies the conceptual family; `PrimitiveProtocolVersion` expresses compatible family protocol revision; `ContractFingerprint` detects semantic-contract disagreement.
- `ContractFingerprint` is 32 bytes (SHA-256).

## 3.4 Command Semantics

- Command means asynchronous intent, not RPC.
- Core Command data is conceptual identity/version/correlation/payload.
- A Command has no intrinsic wait, acceptance, completion, result, response expectation, timeout, transport route/address, or RPC-style request/response contract.
- `ConceptualMessageId` identifies the message; optional `CorrelationId` associates later independent messages.
- Outcomes are expressed through separate reciprocal Command/Event/State concepts as appropriate.
- Local dispatch success means only that the framework accepted local handling.

## 3.5 Event Semantics

- Event means occurrence; transport means travel; local dispatch means processing. These concerns remain separate.
- Intrinsic Event data is EventTypeId, SchemaVersion, MessageId, EventPriority, Payload, and optional CorrelationId.
- Event contains no transport address or HopCount.
- Local/Remote origin is receive/dispatch context, not intrinsic Event state.
- EventPriority is an application/local-dispatch hint and is not Mesh QoS. Mesh adapters map policy where needed.
- Event is one-way and has no intrinsic acknowledgement, response, completion or RPC contract.
- EventDispatchMethod is a local execution choice.
- A received remote Event is not automatically forwarded onward.

## 3.6 State Semantics

- `StateAddress = DeviceIdentifier + StateTypeId`. There is no separate SourceIdentifier.
- Multiple semantic State instances use distinct State definitions/StateTypeIds.
- Local State remains application-owned; State binds/indexes/observes/exposes it without central value copying.
- Remote replicas are State-owned.
- State is read-only outside its authority; Command represents mutation intent.
- One unified registry may represent local/remote State with provenance, while value availability is separate from value itself.
- Availability includes Available/Stale/Unavailable/Expired and reasons such as SourceUnbound/SourceUnreachable.
- Ordinary application members are bindable; typed non-owning binding is preferred and RTTI/`void*` indirection is avoided.
- The owner is responsible for unbinding; RAII binding is preferred where practical.
- The registry is read-only to consumers; the authoritative owner mutates the source.
- `NotifyChanged` controls revision/observation/remote propagation. Direct local reads dereference the current authoritative source.
- Revision belongs to the binding/registration, not to application storage.
- Duplicate local authority for the same State identity is rejected.
- Source abstractions may support direct references, Observable/StateValue sources, or computed accessors. Computed accessors may copy and must not be falsely described as zero-copy.
- Unbind Discard removes metadata and causes future binding to establish a new lineage; Retain preserves inactive metadata/revision lineage.
- No retained binding may preserve dangling source/value references.
- Unbinding propagates Unavailable/SourceUnbound.
- Observers attach to State identity/registration; value, availability and lifecycle observations are distinct.
- Binding-scoped observers terminate with their binding; entry observers may survive retained unbind/rebind.
- Notifications are synchronous/deterministic by default; heavy work should be handed to Task.
- Per-State notification is non-reentrant and serialized; same-State changes arising during notification are deferred and emitted in revision order.
- Observer registrations are bounded and deterministic in registration order.
- State owns State-specific observer semantics. Optional `StateObserverEventBridge` lives in State as optional Event integration.
- Equality/publication policy is application/domain aware, including tolerances where appropriate.
- `StateValue` may suppress publication on equivalent assignment.
- Explicit `NotifyChanged` is authoritative and advances revision even where equality would otherwise consider the value equivalent.
- Reference-backed State does not keep an implicit shadow copy merely to compare equality.
- Equality policy participates in compatibility where it affects distributed revision semantics.
- State transport asynchronously replicates the latest authoritative fact.
- State protocol includes Publication, Availability, Subscribe, Unsubscribe and Resynchronize.
- There is no baseline ReplicaAcknowledgement primitive.
- `StateEpoch` is distinct from `MembershipIncarnation`.
- Revisions are monotonic within an epoch.
- Retain-on-unbind preserves epoch/revision; discard creates a new epoch.
- Coalescing State publications is allowed. Applications needing every transition must model that as Event/stream semantics instead.
- Subscribe/Unsubscribe are asynchronous; establishment/rejection may be expressed as reciprocal State primitives.
- Resynchronize requests current authoritative State, not replay history.
- Generic Disconnect is removed.
- Remote effective availability combines authoritative source availability with Mesh reachability.
- MeshAdapters validates `StateAddress.DeviceIdentifier` against the authenticated Mesh source.

## 3.7 Mesh Primitive and Adapter Model

- Mesh defines a first-class Mesh Primitive family for infrastructure/control-plane traffic. Mesh control traffic is not represented as Command/Event/State.
- Mesh has a common envelope containing extensible `PrimitiveFamilyId` plus opaque payload.
- Mesh core understands only its own Mesh family; other families dispatch to a registered family receiver.
- Application-defined families are supported in 1.0.0.
- There is one active receiver per family.
- Unknown family at the destination still counts as Mesh transport delivery if the envelope reached the destination; unsupported-family disposition is local to the destination.
- `ESPressio-MeshAdapters` provides independently consumable CommandMeshAdapter, EventMeshAdapter, StateMeshAdapter and future family adapters. It does not own the subsystem-specific Observable→Event bridges.
- `PrimitiveReceiveContext` contains only transport-independent provenance. Mesh-specific alias/incarnation/delivery data belongs in `MeshReceiveContext`.
- `MeshObserverEventBridge` belongs in Mesh and is optional on Event.
- `StateObserverEventBridge` belongs in State and is optional on Event.
- Equivalent bridges in other subsystems live with their observable source subsystem.

## 3.8 Primitive Support Compatibility

- `NodeProfile` advertises bounded per-family support descriptors containing PrimitiveFamilyId, supported version range, ContractFingerprint and exposure.
- Primitive support is separate from Mesh membership compatibility.
- Compatible family versions are determined by range intersection; highest mutual version is the default selection.
- Family fingerprint describes the family protocol contract, not every application payload schema.
- Payload/type/schema compatibility is owned by the family.
- Unknown `StateTypeId` does not make the entire State family incompatible.
- Primitive support normally does not affect MeshSignature unless explicitly elevated to a membership-level invariant.
- Registered receiver state is the preferred support source; a receiver may be hidden from advertisement where required.
- Multiple bounded version/contract ranges are permitted.
- Supports / Compatible / Reachable / Authorized are separate concepts and MUST NOT be conflated.

## 3.9 Mesh Identity, Configuration and Security

- One active Mesh per device is the 1.0.0 baseline.
- `MeshIdentifier` is an application-supplied opaque non-zero 128-bit value with exact 16-byte representation. All-zero means Invalid/Unspecified.
- MeshIdentifier is not an arbitrary string on the wire.
- `MeshProtocolVersion` is separate from MeshIdentifier and uses compatible version ranges.
- `MeshSignature` fingerprints distributed semantic truth/compatibility, not firmware/build identity.
- `CanonicalName` is a mandatory human-readable Mesh-unique NodeProfile property.
- CanonicalName length is 1–32 printable ASCII bytes (`0x20–0x7E`), with no embedded NUL/control characters and no leading/trailing spaces; internal spaces are allowed.
- CanonicalName equality/uniqueness is exact and case-sensitive. No locale, case folding or Unicode normalization applies.
- Semantic CanonicalName storage is one `uint8_t` length plus 32 bytes backing = 33 bytes before containing-structure alignment; no trailing NUL is required.
- Changing CanonicalName changes NodeProfile/ProfileGeneration but never DeviceIdentifier or MembershipIncarnation.
- Credentials are separate from MeshIdentifier and NodeProfile and belong in MeshSecurityConfiguration/Security abstractions.
- Node profile claims are trusted only after authentication.
- Admission pipeline is Unknown → Discovered → Authenticating → Validating → Joining → Active.
- Cheap Mesh protocol/MeshIdentifier/MeshSignature checks precede expensive cryptography.
- Any Active member may sponsor a compatible/authenticated candidate; sponsor is not a leader/authority over the Mesh.
- Acceptance makes the member Active immediately; convergence of that fact is asynchronous.
- Bootstrap of the first member is allowed only after no compatible mesh is observed for the configured bootstrap interval.
- `MembershipState` is distinct from `ReachabilityState`.
- `MembershipIncarnation` is a 128-bit participation-instance identifier and is distinct from DeviceIdentifier.
- Network partition does not itself change MembershipIncarnation.
- Local reachability expiry does not become globally authoritative termination.
- Authenticated Leave is authoritative for the participation lifecycle represented by that evidence.
- FastRejoin/continuity evidence never replaces authentication.
- Security authority and admission are abstractions, e.g. IMeshSecurityAuthority / IMeshAdmissionPolicy, with concretes such as shared-credential or provisioned-device admission supplied at application composition level.
- DeviceIdentifier is not proof of authentication.

## 3.10 Membership Tombstones

- Membership tombstones live in a separately bounded local table.
- Default `MaxMembershipTombstones = 64`, independent of `MaxMeshNodes`.
- A tombstone is scoped to `DeviceIdentifier + MembershipIncarnation`.
- Tombstones retain only compact historical participation information required for stale-incarnation/continuity handling: DeviceIdentifier, MembershipIncarnation, local monotonic retention deadline, and compact disposition.
- Disposition distinguishes at least LocallyForgotten, SupersededIncarnation and AuthoritativeLeave.
- Tombstones retain no NodeProfile, CanonicalName, topology, routes, Radio endpoint state, payload or ordinary dedup storage.
- Security may independently retain bounded credential/revocation/session state; Mesh tombstones MUST NOT become a second Security database.
- Default compact tombstone retention is approximately five minutes using local monotonic time.
- Active membership and tombstone pools are separate; tombstones cannot consume active-member capacity and active membership cannot consume tombstone capacity.
- On insertion: remove expired tombstones; update an existing exact identity+incarnation entry in place; use a free slot when available; otherwise deterministically evict and insert the new tombstone.
- Deterministic eviction preference is oldest LocallyForgotten, then oldest SupersededIncarnation, then oldest AuthoritativeLeave; within the same class use retention deadline and stable DeviceIdentifier/MembershipIncarnation ordering as tie-breaks.
- No tombstone is permanently unevictable.
- Early tombstone eviction means loss only of local historical/continuity optimization. A later unknown claimant gets no continuity privilege and must pass normal compatibility, authentication and admission.
- Tombstone exhaustion may increase future authentication work but MUST NOT weaken authentication, create authority, or consume/prevent active-member capacity.
- Current authenticated membership/incarnation authority always outranks tombstone history.

## 3.11 MeshNodeAlias

- `MeshNodeAlias` is a deterministic 16-bit Mesh-local routing handle derived from DeviceIdentifier + MeshIdentifier + collision index.
- Alias collision resolution is deterministic using DeviceIdentifier ordering.
- Alias is not identity or membership authority.
- Membership changes can require alias reassignment.
- Authority is grounded in DeviceIdentifier + MembershipIncarnation so alias churn cannot transfer authority.

## 3.12 Destinations, Group, Capability Selector, Selective Multicast and Broadcast

- Public/application destinations are Node, CapabilitySelector, Group and Broadcast.
- Node normalizes to one independent Node delivery.
- Group resolves exactly once at the sender to a frozen bounded recipient set and then becomes N independent Node deliveries.
- CapabilitySelector resolves exactly once at the sender to a frozen bounded recipient set and then becomes N independent Node deliveries.
- Group is a stable application-defined logical recipient identity self-declared in authenticated NodeProfile; there is no group leader/admin.
- Group-membership changes advance ProfileGeneration.
- State predicates are not destination selectors.
- Selective multicast is one sender-local aggregate operation with one frozen recipient set, one immutable shared payload representation, one aggregate deadline/handle, and independent per-recipient delivery state.
- Every recipient delivery receives its own `MeshMessageId` and independent outcome/receipt lifecycle.
- No wire-level MulticastOperationId is required.
- Payload backing may be shared immutably across recipient deliveries and MUST NOT be copied once per recipient merely because there are multiple recipients.
- Baseline ordinary wire destinations are only Node or Broadcast. Group and CapabilitySelector are not propagated for intermediate re-evaluation.
- Broadcast is one best-effort controlled flood through topology reachable during its finite dissemination lifetime.
- Broadcast has no frozen recipient set, no destination acknowledgement and no promise of "complete to all".
- Applications requiring per-member delivery knowledge must freeze active membership and use selective multicast instead.
- A Broadcast keeps one `MeshMessageId` for the flood.
- Broadcast forwarding uses duplicate suppression and is independent of whether an intermediate node understands the primitive family.
- Broadcast is bounded/backpressured and there is no reliable-Broadcast mode.

## 3.13 Topology and Routing

- Mesh owns end-to-end delivery orchestration; Radio executes only the selected next hop/link transfer.
- Routing behaviour is injectable through abstractions such as `IRoutingStrategy`, `IRouteAttemptPolicy` and `IRetryPolicy`.
- Topology is separate from routing strategy.
- 1.0.0 uses a bounded known link-state model.
- Links are directed; multiple Radios between nodes are represented as distinct edges where appropriate.
- Origin-authenticated topology advertisements are relayed unchanged.
- Topology dissemination is operational/injectable/hierarchical where suitable.
- Capacity compatibility supports Strict vs LowestCommon behaviour; a lower-capacity candidate must never cause eviction of already Active members merely to fit.
- Partition reconciliation treats authenticated individual records as authority; there is no authoritative "partition" object.
- Self-owned records use incarnation + generation.
- Competing incarnations are a conflict requiring reconciliation.
- CanonicalName conflict uses the locked hierarchical conflict policy.
- Reconciliation summary is injectable and bounded; the baseline is a bounded summary table.
- Existing Mesh issues include QoS-aware pluggable routing strategy (#1) and injectable partition reconciliation summary strategies (#2).

## 3.14 Retries, Deduplication and Lifetimes

- Default route-attempt policy allows maximum 3 same-route attempts and maximum 4 routes.
- Default deduplication window is 128 sequence positions.
- Dedup core is 8-byte highest-observed sequence plus 16-byte bitmap = 24 bytes excluding member key.
- Full unreachable member record retention default is 60 seconds.
- Compact tombstone retention default is approximately 5 minutes.
- All work/queues/lifetimes are finite and bounded.
- A retry/re-route of one delivery preserves that delivery's immutable logical bytes and MessageId.

## 3.15 Clock Synchronization

- There are no configured permanent clock roles; every eligible node is elective.
- Timing owns clock math/discipline/quality; Radio owns direct-link timestamp mechanics/capability/uncertainty; Mesh owns global root election, direct time-parent choice, stratum/quality propagation and partition behaviour.
- Synchronization is hierarchical via direct links; data route and time-parent route may differ.
- Policy abstractions include IClockQualityPolicy, IClockEligibilityPolicy, IClockRootElectionPolicy and IClockParentSelectionPolicy.
- Root election is quality-first with deterministic DeviceIdentifier tie-break.
- Consensus-affecting clock policies participate in MeshSignature; ordinary parent-selection mechanics do not unless they affect distributed truth.
- Radio timing is optional and uncertainty is explicit.
- Link-local precision exchange uses T1/T2/T3/T4 plus uncertainty/link-quality information.
- Precision exchange bypasses ordinary Mesh routing, fragmentation and application/control queues where required to preserve timing fidelity.
- Clock advertisements are informational; nodes independently calculate the root.
- Loop prevention requires same-root ancestry and strictly improving stratum.
- Convergence is asynchronous and supports degraded/holdover/unsynchronized states.

## 3.16 Traffic Governance and Protected Classes

- Mesh traffic governance is injectable through `IMeshTrafficGovernor` at application composition level.
- Universal invariant: eligible control-plane traffic must retain serviceability ahead of saturating application traffic.
- Default `ReservedPriorityTrafficGovernor` capacities are: Infrastructure Responses 8, Clock Control 4, General Control 8, Application Transmissions 8.
- Clock Control has dedicated capacity plus preferential/deadline-aware scheduling.
- Infrastructure Responses and Clock Control are both critical; there is no rigid rule that every clock message always outranks every delivery acknowledgement.
- Application traffic cannot consume protected control reserves.
- Control may borrow otherwise idle general/application capacity where doing so is safe and does not remove guaranteed reserve.
- Precision T1/T2/T3/T4 is separately protected at the Radio/Timing boundary.
- Clock control should remain compact and coalescible.
- Mandatory responses such as delivery acknowledgements must remain serviceable under application saturation.
- Control-work lifetime is finite and injectable through `IControlWorkLifetimePolicy`.
- Operational governor/lifetime configuration normally does not participate in MeshSignature.

## 3.17 Execution and Concurrency

- Mesh is decomposed into narrow services rather than one giant worker/object.
- Distinct semantic responsibility does not imply a distinct FreeRTOS task/thread.
- Each subsystem owns its mutable state and exposes bounded views.
- The architecture is reactive.
- Internal Mesh signaling is not modeled as conceptual Event unless it genuinely represents an application/domain occurrence.
- Prefer ESPressio PrecisionThread/Task abstractions rather than direct RTOS calls.
- Baseline implementation may use one shared serialized Mesh execution domain/maintenance worker with services exposing NextMaintenanceDeadline/PerformMaintenance and WakeForWork-style signalling.
- A dedicated receive thread should not be introduced unless the Radio concrete requires it.
- All queues are bounded and expose explicit backpressure.

## 3.18 Payload Ownership

Mesh supports three logical payload forms:

1. Borrowed Stable Payload.
2. Bounded Owned Payload.
3. Repeatable Serialized Source.

Rules:

- Finite logical length MUST be known before acceptance.
- One `MeshMessageId` maps to one immutable logical byte representation for one independently routable Node delivery or one Broadcast.
- Retries, alternate routes and Radio fragments request identical ranges from that immutable logical representation.
- Selective-multicast recipient deliveries may share immutable payload backing without sharing MessageId.
- Mutable reserialization between retries is forbidden.
- State may coalesce before commit; once a logical payload is committed to Mesh it is immutable.
- Adapters own primitive-family serialization; Mesh owns envelope/end-to-end delivery; Radio owns packetization/link fragmentation.

## 3.19 Radio Logical Transfer and Fragmentation

- Radio implementations own bounded hop-local fragmentation/reassembly required to carry a Mesh logical transfer over their physical frames.
- `MaximumLogicalTransferSize` is a finite Radio capability and may be larger than one physical packet.
- Mesh must not understand technology-specific fragmentation details.
- Existing ESP32 Raw80211 currently handles a physical payload around 270 bytes and therefore requires bounded logical fragmentation/reassembly to carry mandatory Mesh control snapshots that can exceed one frame.
- Concrete Radio TX/reassembly capacities are implementation-owned but MUST be finite, bounded, documented and included in memory accounting.

## 3.20 ESP32 Shared Physical Wi-Fi PHY Coexistence

- A physical Radio resource shared by multiple platform services has one platform-level owner/arbitrator for hardware-global state.
- On ESP32, ordinary Wi-Fi and Raw80211Radio share the integrated 802.11 PHY and MUST NOT independently retune/reconfigure global PHY state.
- Raw80211Radio defaults to coexistence by following the current effective shared Wi-Fi channel.
- A fixed Raw80211 channel may constrain the PHY only when the shared PHY is otherwise unconstrained.
- If an active service requires an incompatible channel/state, Raw80211 reports restriction/unavailability rather than disrupting the other service.
- Wi-Fi association/reassociation channel changes produce Radio/link availability/topology convergence; they do not change device identity or Mesh membership identity.
- Mesh itself has no ESP32 channel semantics.
- Nodes on incompatible channels simply lack that direct Raw80211 edge and may route through other compatible members.
- `ESPressio-ESP32` owns this shared-PHY coordination because it implements both portable abstractions and knows the hardware-sharing relationship.
- Shared global Wi-Fi controls such as power-save and transmit power must be audited under the same ownership model.

## 3.21 Persistence Composition

- Persistence remains a separately owned functional abstraction and must be composed/injected rather than reimplemented inside Mesh.
- Persisted configuration/state must preserve the same ownership, boundedness and semantic contracts as live runtime structures.
- Persistence policy/configuration only participates in MeshSignature where it changes distributed authoritative truth; storage mechanics do not.

## 3.22 Policy Contract / MeshSignature Rule

- Any policy or policy tree whose configuration can alter distributed authoritative truth contributes to MeshSignature.
- Semantic policy identity is represented recursively through stable PolicyIdentifier, ContractVersion, ConfigurationSignature and DelegateContract information.
- RTTI names, compiler names, object layout, function addresses and implementation-build artifacts MUST NOT be used as semantic policy identity.
- Operational mechanisms that do not affect distributed truth normally do not participate in MeshSignature.

## 3.23 Fixed Default Capacities and Core Sizes

Locked default bounds:

- `MaxMeshNodes = 32`
- `MaxRadiosPerNode = 4`
- `MaxGroupsPerNode = 8`
- `MaxPrimitiveReceivers = 8`
- `MaxActiveApplicationTransmissions = 8`
- `MaxTopologyLinks = 96`
- `MaxRouteHops = 16`
- `DefaultHopLimit = 16`
- route cache entries = 32
- `MaxActiveInboundDeliveries = 8`
- `MaxPendingNeighbourCandidates = 8`
- `MaxActiveLivenessProbes = 4`
- `MaxActiveInboundAuthentications = 4`
- `MaxRecipientsPerTransmission = 32`
- `MaxMembershipTombstones = 64`
- Infrastructure Response protected capacity = 8
- Clock Control protected capacity = 4
- General Control protected capacity = 8
- Application Transmission protected capacity = 8
- full unreachable member retention = 60 s
- compact tombstone retention ≈ 5 min
- same-route attempts = 3
- routes attempted = 4
- dedup window = 128

Locked/known semantic sizes:

- `DeviceIdentifier` = 16 B
- `MeshIdentifier` = 16 B
- `MembershipIncarnation` = 16 B
- `ContractFingerprint` = 32 B
- `MeshMessageId` = 8 B
- `ProfileGeneration` = 8 B
- `TopologyGeneration` = 8 B
- `PrimitiveFamilyId` = 2 B
- `MeshNodeAlias` = 2 B
- `RadioIdentifier` = 1 B
- `RemainingHopLimit` = 1 B
- default CapabilityMask = 8 B
- CanonicalName semantic backing = 33 B before containing-structure alignment
- default dedup core excluding key = 24 B
- `StateTypeId` = 8 B
- `StateRevision` = 8 B
- `StateEpoch` = 4 B

## 3.24 Provisional No-PSRAM Memory Model

The final architecture audit established a conservative pre-implementation fixed/cardinality metadata budget of approximately **60,160 bytes (58.75 KiB)** with all default fixed cardinalities occupied, including the 64-entry membership tombstone table budgeted conservatively at 48 B per entry.

Using the current ESPressio threading defaults as the implementation starting point adds approximately **10,000 bytes (9.77 KiB)** of configured task-stack reservation (two ordinary 4,000-byte tasks plus the 2,000-byte termination dispatcher), producing a fixed starting floor of approximately **70,160 bytes (68.52 KiB)** before variable payload/control/reassembly/runtime overhead.

Ordinary/application payload ownership is bounded by approximately `20 × Papp` under full occupancy: 8 outbound immutable logical payloads + 8 inbound owned deliveries + 4 pre-authentication owned bodies. Selective multicast does not multiply payload copies by 32 recipients.

Planning equation:

`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`

where:

- `Papp` = maximum retained ordinary logical Mesh payload bytes;
- `Pinfra` = maximum retained Infrastructure Response body;
- `Pclock` = maximum retained Clock Control body;
- `Pgeneral` = maximum retained General Control body;
- `R` = concrete Radio TX/reassembly pools;
- `C` = transactional control-snapshot staging;
- `A` = allocator/container/FreeRTOS overhead;
- `X` = Security/session-provider private storage.

The absolute whole-device DRAM value cannot be a single architecture constant because concrete Radio transfer/reassembly sizes, control snapshot sizes and runtime implementation overhead are implementation-owned. They MUST nevertheless be finite, bounded and measured.

Indicative ordinary-payload scenarios before those extra terms:

- `Papp = 270 B` → ≈ 73.79 KiB
- `Papp = 512 B` → ≈ 78.52 KiB
- `Papp = 1 KiB` → ≈ 88.52 KiB
- `Papp = 2 KiB` → ≈ 108.52 KiB
- `Papp = 4 KiB` → ≈ 148.52 KiB

No-PSRAM ESP32 builds must assume ExternalPreferred allocations fall back into internal DRAM when SPIRAM is unavailable.

## 3.25 Final Architecture Status

**ARCHITECTURE AUDIT: PASS — IMPLEMENTATION READY.**

The reconciled architecture through Decisions 1–50D is internally coherent. The final confirmation audit found no remaining architecture-level contradiction, dependency cycle, unbounded retained resource, identity ambiguity, delivery/deduplication conflict, security contradiction, clock contradiction, persistence contradiction, execution contradiction, or shared-physical-radio ownership conflict.

Implementation is authorized by Simon as of this tranche. The frozen specification above may not be changed without his explicit approval.

---

# 4. Implementation Guardrails

During implementation, an agent may make ordinary internal implementation choices only when those choices do not change the frozen public/distributed specification. The agent MUST stop and request Simon's decision before proceeding if a choice:

- changes a public API or distributed semantic;
- changes repository ownership or introduces a new dependency not already implied by the architecture;
- introduces or risks a circular dependency;
- changes a locked capacity/default or bounded-memory guarantee;
- changes wire compatibility, authentication, identity, membership, incarnation, generation, routing authority or MeshSignature-relevant behaviour;
- requires deviation from Policy;
- reveals an architecture infeasibility/contradiction;
- would require reinterpretation rather than implementation of the frozen specification.

Implementation-owned details such as exact C++ container/layout choices, exact bounded Radio reassembly/TX capacities, concrete control-snapshot byte capacities and internal task decomposition may be chosen during the tranche, but they must remain finite/bounded, preserve the frozen semantics, follow ESPressio abstraction policy, be documented, and be included in validation/memory accounting.

---

# 5. Current Implementation State

Foundation implementation is healthy. `ESPressio-System/structural_realignment_propagation_ESPressio-Mesh` owns the canonical exact 16-byte `DeviceIdentifier`; `ESPressio-Primitive/structural_realignment_propagation_ESPressio-Mesh` owns the dependency-neutral conceptual-message vocabulary and central family-ID namespace; and `ESPressio-Mesh/structural_realignment_propagation` contains the 1.0.0 scaffolding, frozen Mesh identity/value types and fixed default capacity constants. Concrete ESPressio Primitive family IDs remain deliberately unallocated because their stable numeric values are public/wire contract and were not frozen by Decisions 1–50D.

The Radio tranche is validated. `ESPressio-Radio/structural_realignment_propagation_ESPressio-Mesh` now owns only bounded direct-link logical transfer over caller-selected `IRadio`/next-hop `RadioAddress`, including finite fragmentation/reassembly and recent-transfer suppression. It no longer owns Mesh node routing, heterogeneous forwarding, Mesh hop limits or conceptual-message discrimination. The generic logical-transfer ceiling is 4096 bytes, with four bounded reassembly slots and 32 bounded recently-completed transfer records. Radio Actions run `33763062738` passed native direct-link and clock tests plus ESP32 smoke compilation.

The ESP32 shared-PHY tranche is validated. `ESPressio-ESP32/structural_realignment_propagation_ESPressio-Mesh` contains the platform-level Wi-Fi PHY coordinator. Ordinary Wi-Fi owns active global PHY policy; Raw80211 follows the effective channel unless the PHY is otherwise unconstrained, and incompatible Raw operation reports Busy/unavailable rather than retuning active Wi-Fi. Global transmit-power and power-save writes pass through the coordinator. Raw80211 advertises a 4096-byte logical Radio transfer ceiling over its ~270-byte physical frame payload. Initial CI failed while ProviderSmoke still used the previous Radio API; corrected run `33764387563` passed.

The NRF24 Radio concrete is validated. `ESPressio-NRF24/structural_realignment_propagation_ESPressio-Mesh` advertises the same finite 4096-byte logical transfer ceiling over its 32-byte physical payload while preserving the separate atomic direct-clock exchange. Physical receive Source remains intentionally invalid; ordinary logical-transfer sender identity is reconstructed from Radio-owned framing. CI run `33764610171` passed.

`ESPressio-State/structural_realignment_propagation_ESPressio-Mesh` has now completed its identity/protocol/availability migration and its local-authority realignment. State aliases the canonical System `DeviceIdentifier`; `StateAddress` is exactly `DeviceIdentifier + StateTypeId`; MAC-specific identity helpers are gone from the State API. The previous acknowledgement-driven publication lifecycle and generic Disconnect protocol have been removed. `StatePublicationTracker` is latest-only. Protocol v2 separates Publication, Availability, Subscribe, Unsubscribe, Resynchronize, SubscribeResult and UnsubscribeResult. The 30-byte Availability wire form carries only source-authoritative State availability/reason; `SourceUnreachable` is rejected on that wire path because reachability is transport/Mesh-derived local context.

Remote State availability is decomposed from reachability. `RemoteStateManager` stores source-authoritative availability per State, stores `StateSourceReachability` separately per device, computes effective `StateAvailabilityStatus`, and emits deterministic effective-availability transitions when reachability changes. Introspection, diagnostic serialization and deferred typed observation expose availability and reachability independently. Remote manager device capacity and lifecycle-observer capacity are now separately finite.

Local State authority is unified through `LocalStateRegistry<TContract>` and `StatePublisher<TContract,...>`. Application/domain storage remains canonical; State retains typed non-owning references and registration metadata rather than a second canonical value table. Initial bind starts revision 1; Retain preserves epoch/revision and a later rebind advances revision before exposing a replacement source; Discard causes the next bind to begin a new epoch/revision-1 lineage. Explicit `NotifyChanged` owns revision advancement and publication regardless of equality, while `Snapshot` reads the current source without changing lineage. Bind/unbind propagate source lifecycle and `Available` versus `Unavailable/SourceUnbound` separately from value publication. The obsolete callback-source/`RegisterSource` publisher path and LastPublished shadow values have been removed.

State observer memory is now bounded explicitly. `StatePublisher`, `RemoteStateManager`, `StateSubscriptionRegistry`, `StateSubscriberRegistry`, and optional `RemoteStateObserverThread` each have an independent compile-time observer-registration maximum, defaulting to 8 while preserving their separate data capacities. Registration slots are reusable after RAII handle release and deterministic callback order remains delegated to ESPressio Observable.

The local publication path now enforces the frozen same-State non-reentrant behavior for value publication. If `NotifyChanged` is called for the same State from inside its current publication observer, it never recursively enters those observers. StatePublisher retains at most one immutable externally-preferred pending snapshot for that State; subsequent nested revisions may replace it before dispatch, coalescing obsolete intermediate revisions as permitted for latest-fact State. Emitted revisions remain strictly increasing. A dedicated regression verifies observer depth remains one and a nested revision 3 may be superseded by revision 4 while revision authority reaches 4.

State validation remains healthy and GitHub Actions remains usable; there is no task-budget exhaustion symptom. Run `33769640402` passed host, ESP32 core, ESP32 diagnostics and ESP32 observer-thread builds after bounded observer registration was applied. Run `33770242447` passed all four jobs including the new same-State publisher reentrancy/coalescing regression. Documentation has been updated to the unified authority, bounded observer and serialized publication semantics.

State is still in self-audit rather than declared fully complete. The next audit focus is whether source binding/unbinding/availability lifecycle callbacks and low-level RemoteStateManager lifecycle callbacks also need State-owned non-reentrant sequencing beyond the already-protected typed publication/application-observer path; this must be resolved against the frozen per-State notification rule without modifying generic Observable semantics or introducing unbounded event history. The audit must also re-check exception/backpressure behavior, comments, stale source/API terminology, dependency pins, and every remaining State-owned retained collection.

Immediate next work: complete that State recursive self-audit, correct any remaining boundedness/reentrancy/documentation defects, and obtain one final clean State CI baseline. Only then mark State structurally complete. After State, propagate Primitive vocabulary into Command/Event/State and proceed toward MeshAdapters and higher-order Mesh services. Concrete ESPressio Primitive family IDs remain a deliberate stop/decision boundary because the architecture defines their namespace but not their actual stable numeric allocations.

---

# 6. Chronological UTC Work Log

- **2026-09-03T12:59:05Z** — Tranche authorized by Simon. Primary implementation context confirmed as `ESPressio-Mesh`; frozen architecture status is PASS / IMPLEMENTATION READY.
- **2026-09-03T12:59:05Z** — Created `ESPressio-Mesh` branch `structural_realignment_propagation` from current Working Branch `structural_realignment` in accordance with cross-library propagation policy.
- **2026-09-03T12:59:05Z** — Created `ESPressio-Primitive`, `ESPressio-System` and `ESPressio-Radio` propagation branches from their `structural_realignment` Working Branches. Repository correction remains singular `ESPressio-Primitive`.
- **2026-09-03T12:59:05Z** — Created this authoritative `TRANCHE_HANDOFF.MD` on the primary context propagation branch.
- **2026-09-03T13:13:44Z** — Implemented canonical `ESPressio::System::DeviceIdentifier`: exact 16-byte fixed representation, all-zero Invalid/Unspecified, immutable bytes, validity, equality and deterministic ordering; no hardware/MAC derivation in System. System regression and CI passed.
- **2026-09-03T13:13:44Z** — Implemented initial `ESPressio-Primitive` dependency-neutral vocabulary and central family namespace boundaries. No concrete ESPressio family IDs were allocated. Primitive regression and CI passed.
- **2026-09-03T13:22:31Z** — Created matching Mesh propagation branches for `ESPressio-ESP32`, `ESPressio-NRF24` and `ESPressio-State`; repinned participating dependencies to propagation branches.
- **2026-09-03T13:22:31Z** — Added initial Mesh manifest/scaffolding, frozen fixed-width Mesh identity/value types and locked default capacities; local Mesh regression passed.
- **2026-09-03T13:47:30Z** — Structurally realigned `ESPressio-Radio` to direct-link logical transfer with bounded fragmentation/reassembly and Radio-owned source-address framing. Set implementation logical-transfer ceiling to 4096 bytes, four reassembly slots and 32 recent-completion records.
- **2026-09-03T13:57:30Z** — Radio CI run `33763062738` passed native direct-link transport tests, direct clock protocol tests and ESP32 smoke compilation.
- **2026-09-03T13:57:30Z** — Added ESP32 `WiFiPhyCoordinator`, reconciled ordinary Wi-Fi/Raw80211 shared-PHY ownership, and advertised Raw80211 4096-byte logical transfer capability.
- **2026-09-03T14:01:02Z** — ESP32 initial propagation CI exposed stale ProviderSmoke use of the previous Radio API. ProviderSmoke was corrected to the direct-link API; follow-up run `33764387563` passed.
- **2026-09-03T14:03:32Z** — NRF24 propagation CI run `33764610171` passed after advertising the bounded 4096-byte logical transfer ceiling while preserving 32-byte physical transport and separate direct-clock behavior.
- **2026-09-03T14:09:00Z** — Began State structural migration. Re-pinned System to its propagation branch; replaced State-owned/MAC-derived DeviceIdentifier with the canonical System identity alias; removed baseline State publication acknowledgement lifecycle and generic Disconnect protocol semantics; changed outbound publication tracking to latest-fact coalescing.
- **2026-09-03T14:10:00Z** — First State CI after identity migration exposed a self-include collision in the transitional DeviceIdentifier facade and stale MAC-specific/acknowledgement tests. The facade was corrected through the System umbrella and tests were rewritten around canonical fixed identity and latest-fact semantics.
- **2026-09-03T14:14:00Z** — State CI reached a clean four-job baseline after the initial identity/protocol migration: host regressions plus ESP32 core, diagnostics and deferred-observer smoke builds passed.
- **2026-09-03T14:18:00Z** — Added canonical `StateAddress`, State availability/reachability types and effective-availability resolution. Reworked `RemoteStateManager` to separate authoritative State availability from device reachability and updated manager/typed/deferred observer contracts.
- **2026-09-03T14:22:00Z** — Updated State introspection and diagnostic serialization to expose effective State availability and reachability separately. Updated CI smoke sources to use the new observer contracts. Host/ESP32 validation passed after stale introspection tests were reconciled.
- **2026-09-03T14:27:24Z** — Corrected reachability handling so a device reachability transition also produces deterministic per-State effective-availability transitions for each retained State identity, using compile-time bounded transition storage.
- **2026-09-03T14:31:20Z** — Added `LocalStateRegistry<TContract>` with typed non-owning application-owned binding, duplicate-authority rejection, registration-owned epoch/revision, explicit NotifyChanged, Retain/Discard lineage semantics and explicit-mode RAII unbinding. Added host regression coverage. Run `33767340818` passed all four State CI jobs.
- **2026-09-03T14:35:47Z** — Completed explicit State Availability wire semantics and validation. `SourceUnreachable` is rejected as authoritative wire reason because reachability is derived locally from transport/Mesh context. State protocol regression covers the separation. Run `33767783304` passed host, ESP32 core, diagnostics and observer-thread jobs.
- **2026-09-03T14:38:00Z** — Reconciled this handoff to current absolute truth. State core was validated; local publisher/registry/lifecycle integration and bounded observer enforcement remained open.
- **2026-09-03T14:45:00Z** — Replaced the old `StatePublisher` callback-source/LastPublished/revision path with the single `LocalStateRegistry` authority. Added Bind/Unbind/NotifyChanged/Snapshot semantics, explicit authoritative availability lifecycle, retained-rebind revision advancement, and updated publisher observer contracts/tests/README. The first CI attempt exposed only stale tests still calling removed `RegisterSource`/`Publish` APIs; those tests were rewritten around explicit binding/NotifyChanged and callback-lock-boundary behavior.
- **2026-09-03T14:53:00Z** — Enforced independent finite observer-registration capacities across `StatePublisher`, `RemoteStateManager`, `StateSubscriptionRegistry`, `StateSubscriberRegistry`, and `RemoteStateObserverThread`, defaulting to 8. Added regression proving capacity rejection and deterministic slot reuse after RAII handle release. State CI run `33769640402` passed host, ESP32 core, diagnostics and observer-thread jobs. No task-budget symptom occurred.
- **2026-09-03T15:01:18Z** — Added State-owned same-State publication serialization. Nested `NotifyChanged` calls no longer recursively enter typed/generic publication observers; at most one immutable ExternalPreferred pending snapshot is retained per State and obsolete pending revisions are coalesced to the newest revision. Added dedicated regression proving callback depth remains one and revision order remains monotonic under nested updates. CI run `33770242447` passed all four jobs.
- **2026-09-03T15:02:04Z** — Reconciled State documentation and this handoff with the unified local-authority, bounded-observer and non-reentrant publication implementation. State remains under recursive self-audit for lifecycle/manager notification sequencing and final boundedness/dependency/comment validation before being marked complete.
