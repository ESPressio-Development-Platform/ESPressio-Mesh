# ESPressio Mesh Implementation Tranche — Authoritative Handoff

## Status

**TRANCHE IN PROGRESS — ARCHITECTURE PASS / IMPLEMENTATION READY. SYSTEM / PRIMITIVE / RADIO / ESP32 / NRF24 / STATE / COMMAND RPC-REMOVAL / EVENT PROVENANCE VALIDATED. MESH MEMBERSHIP, DELIVERY/DEDUP, RETENTION, LIVENESS, PRE-AUTH ADMISSION, AUTHENTICATED PROMOTION, RADIO PEER/DISCOVERY LIFECYCLE, EXTERNAL PRIMITIVE RECEIVERS, DETERMINISTIC ADVERTISED PRIMITIVE-SUPPORT SNAPSHOTS, PROTECTED TRAFFIC GOVERNANCE, FINITE CONTROL-WORK LIFETIME AND POLICY-DRIVEN BOUNDED ACTIVE LIVENESS PROBING ARE VALIDATED.**

This file is the authoritative implementation-tranche handoff for the tranche originating in `ESPressio-Mesh`. The architecture below is frozen. Implementation may choose bounded internal mechanics only where they do not alter public/distributed semantics, ownership, dependency direction, wire compatibility, authentication/identity semantics, locked bounds or Policy. If a genuine architectural omission/conflict is exposed, stop and ask Simon Stuart.

**Explicit unresolved public/wire decision boundaries:** concrete ESPressio-defined `PrimitiveFamilyId` allocations; distributed EventTypeId assignment; conceptual Event message-ID generation scope; Group identifier contract. Do not invent any of them.

---

# 1. Scope and branches

Primary:
- `ESPressio-Mesh`: `structural_realignment_propagation`

Affected propagation branches:
- `ESPressio-System`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Primitive`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Radio`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-ESP32`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-NRF24`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-State`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Command`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Event`: `structural_realignment_propagation_ESPressio-Mesh`

`ESPressio-MeshAdapters` is architecturally required but not yet implemented/branched in this tranche. Create/verify its matching propagation branch when adapter work starts.

Authoritative correction from Simon: the repository is **`ESPressio-Primitive` (singular)**. Policy references to `ESPressio-Primitives` are typos.

---

# 2. Mandatory Policy rules

- Ordinary libraries must not call platform/hardware APIs directly. Use the appropriate ESPressio abstraction; hardware concretes belong in platform/technology repositories.
- Reuse existing ESPressio abstractions instead of duplicating them.
- Avoid all circular dependencies, including optional ones. If one appears necessary, stop and ask Simon.
- Keep cross-library propagation isolated to the branches above; participating manifests, jobs/tests, examples, docs and source dependency references must target matching propagation branches.
- Add useful Arduino IDE and PlatformIO demos for public-facing discrete components where appropriate.
- Keep docs and IDE-compatible doc comments synchronized with changed APIs.
- If Actions begins failing almost immediately because the monthly task/job budget is exhausted, record that explicitly and stop treating CI as correctness evidence until reset; switch to recursive self-review. **No budget-exhaustion symptom has occurred.**
- Internal diagnostics use ESPressio-Logging and remain compile-switchable; inspect current Logging API before instrumenting.
- Optional callbacks use ESPressio-Observable multi-observer semantics; optional Observable→Event bridges live with their source subsystem.
- Maintain this `TRANCHE_HANDOFF.MD` on the operative Mesh branch while incomplete; update after meaningful branch/dependency/code/validation/decision/correction changes. At full completion offer the final handoff to Simon for download, then delete it from Git.
- Do not change version numbering merely because of tranche work.
- Do not preserve legacy behavior unless Simon explicitly requests it.

---

# 3. Frozen architecture

## 3.1 Dependency/layering

- `ESPressio-System`: foundational platform-neutral identities/types/services; owns canonical `DeviceIdentifier`.
- `ESPressio-Primitive`: dependency-neutral conceptual-message vocabulary only (`ConceptualMessageId`, `CorrelationId`, `PrimitiveFamilyId`, `PrimitiveProtocolVersion`, `ContractFingerprint`, minimal helpers/contracts).
- `ESPressio-Command`: asynchronous intent, not RPC.
- `ESPressio-Event`: occurrence semantics.
- `ESPressio-State`: authoritative fact/replication semantics.
- `ESPressio-Radio`: opaque direct-link physical/link transport and link-local mechanics.
- `ESPressio-Mesh`: membership, topology, routing, control plane, logical addressing, admission, multi-radio forwarding and end-to-end delivery. Mesh must not depend on Command/Event/State.
- `ESPressio-MeshAdapters`: optional Mesh↔Command/Event/State integration above both sides.
- `ESPressio-Timing`: synchronization mathematics/discipline/quality; Mesh owns distributed root/parent coordination, Radio direct-link timestamp mechanics.

## 3.2 Identity and fixed types

- `DeviceIdentifier`: exact 16 B, nonzero valid, System-owned, permanent/transport-independent, never authentication.
- `MeshIdentifier`: exact nonzero 16 B application-supplied Mesh identity.
- `MembershipIncarnation`: exact nonzero 16 B participation-instance identity; partition/radio restart does not inherently change it. FastRejoin may retain it only with authenticated continuity evidence.
- `MeshNodeAlias`: uint16 Mesh-local routing handle only.
- `RadioIdentifier`: uint8; 0 invalid, 1–254 usable, 255 reserved; node-local and unrecycled during one incarnation.
- `MeshMessageId`: uint64, nonzero, monotonic/nonwrapping, scoped by authenticated source DeviceIdentifier + MembershipIncarnation. Retries/routes/fragments retain it; selective-multicast recipients each get one; Broadcast retains one throughout flood.
- `ProfileGeneration`, `TopologyGeneration`: uint64 nonzero, incarnation-scoped, begin at 1 and never wrap.
- `CapabilityMask`: default uint64.
- `CanonicalName`: mandatory Mesh-unique profile property; 1–32 printable ASCII bytes, no NUL/control, no leading/trailing spaces, exact case-sensitive comparison; semantic storage one length byte + 32 backing bytes.
- `ContractFingerprint`: exact 32-B SHA-256 semantic compatibility digest, never authentication.

## 3.3 Primitive family vocabulary

`PrimitiveFamilyId` is exact uint16:
- `0x0000` invalid
- `0x0001–0x7FFF` ESPressio-defined, individually allocated centrally in Primitive
- `0x8000–0xFFFE` application/private
- `0xFFFF` reserved

Version compatibility uses bounded inclusive ranges and normally selects the highest mutual version. Supports / Compatible / Reachable / Authorized are distinct. **No concrete ESPressio family IDs have been allocated in this tranche.**

## 3.4 Command

Command represents asynchronous intent. It has no intrinsic waiting, acceptance response, requested-operation completion/result, timeout, destination/route or reply semantics. Local handler/registry disposition means only what the local framework did with the invocation; it never means the requested real-world operation completed.

## 3.5 Event

Event represents an occurrence. It contains no transport address or HopCount. Local/Remote origin belongs to `EventDispatchContext`, not Event object state. EventPriority is a local/application dispatch hint, not Mesh QoS. Event is one-way and has no intrinsic ACK/response/completion/RPC contract; a remote Event is not automatically re-forwarded.

Frozen intrinsic metadata is EventTypeId, SchemaVersion, conceptual MessageId, EventPriority, payload and optional CorrelationId. Existing stable-name FNV transport TypeID is **not** automatically the distributed EventTypeId; this remains an explicit Simon decision.

## 3.6 State

`StateAddress = DeviceIdentifier + StateTypeId`. Local State remains application-owned and is bound/indexed/observed non-owningly; remote replicas are State-owned. State is read-only outside authority; Command represents mutation intent. Availability is separate from value: Available/Stale/Unavailable/Expired with reasons including SourceUnbound and locally derived SourceUnreachable. `NotifyChanged` advances revision/observation/propagation even when equality says equivalent. Retain preserves inactive lineage; Discard removes it. Same-State observation is bounded, serialized and non-reentrant. Protocol concepts: Publication, Availability, Subscribe, Unsubscribe, Resynchronize; no ReplicaAcknowledgement or generic Disconnect. StateEpoch is independent from MembershipIncarnation. MeshAdapters validates StateAddress.DeviceIdentifier against authenticated Mesh source.

## 3.7 Primitive receivers and profile support

Mesh has its own control Primitive family; its numeric family ID remains unallocated. External families remain opaque and terminate in exactly one active receiver per PrimitiveFamilyId. Unsupported family is a local disposition and does not negate successful Mesh delivery. Receiver handoff is short, bounded and transport-independent.

Authenticated NodeProfile ultimately contains DeviceIdentifier, CanonicalName, CapabilityMask, bounded Groups, primitive support descriptors and ProfileGeneration. Primitive support descriptors carry family ID, supported version range and fingerprint. Full NodeProfile remains blocked by the unresolved Group identifier contract; do not invent it.

## 3.8 Membership, admission, security and liveness

Admission progression: Unknown → Discovered → Authenticating → Validating → Joining → Active. Cheap compatibility checks precede expensive crypto. Any Active member may sponsor; sponsor is not leader. MembershipState and ReachabilityState are distinct. Authenticated Leave is authoritative; reachability expiry is local evidence only. DeviceIdentifier is not authentication. Old-incarnation traffic cannot mutate current authority/dedup state.

Security/admission remain behind injected abstractions such as `IMeshSecurityAuthority` / `IMeshAdmissionPolicy`; Mesh itself must not implement cryptography. Direct-neighbour authentication and end-to-end origin authentication are distinct.

Reachability is Unknown / Reachable / Suspect / Unreachable via injected liveness policy. Approximate baseline floors are ~5 s to Suspect and ~15 s to Unreachable, not universal constants. Any valid authenticated Mesh evidence may restore Reachable immediately. Active probes are used only when passive/control evidence is insufficient; probe eligibility/cadence is injectable and active probe capacity is bounded to 4.

## 3.9 Tombstones

`MaxMembershipTombstones=64`, independent from `MaxMeshNodes=32`. Tombstones contain only DeviceIdentifier, MembershipIncarnation, local monotonic retention deadline and disposition: LocallyForgotten / SupersededIncarnation / AuthoritativeLeave. No profile/topology/routes/radios/payload/dedup. Default retention ~5 min. Insertion purges expired, refreshes exact, uses free slot, otherwise deterministically evicts oldest LocallyForgotten, then SupersededIncarnation, then AuthoritativeLeave; tie by deadline then stable identity. No tombstone is unevictable; early eviction loses only continuity optimization and never weakens authentication/admission.

## 3.10 Delivery and dedup

API destinations: Node, Group, CapabilitySelector, Broadcast. Group/CapabilitySelector resolve once at sender to a frozen bounded recipient set and become N independent Node deliveries sharing immutable payload backing if useful. Broadcast is one bounded best-effort controlled flood with one MeshMessageId, no frozen recipient set, no destination ACK and no complete-to-all promise. Delivery ACK confirms destination Mesh delivery only, not semantic execution.

Dedup is per authenticated source DeviceIdentifier + MembershipIncarnation using HighestObservedSequence + compact 128-position sliding bitmap. Skipped lower sequences remain admissible while within the window; Seen=Duplicate; below retained floor=TooOld and never delivered upward. Dedup happens only after sufficient authentication/incarnation validation. Unseen work first reserves bounded `InProgress`; definitive disposition commits dedup, while TemporarilyUnavailable/ResourceUnavailable releases without commit. Concurrent route/radio copies cannot both dispatch upward. Dedup is never silently disabled under pressure.

## 3.11 Topology, routing and retries

Mesh owns orchestration; Radio executes selected next-hop work. Routing/attempt/retry behavior is injectable (`IRoutingStrategy`, `IRouteAttemptPolicy`, `IRetryPolicy`). Directed link-state default bound is 96 links. Route cache 32, MaxRouteHops 16, DefaultHopLimit 16. TopologyGeneration versions the complete semantic outbound-link set and advances only on material published changes. Forwarding is adaptive hop-by-hop; RemainingHopLimit decrements only on a successful Mesh forwarding transition. Radio owns physical retry. Default Mesh safety ceilings: 3 same-route attempts and 4 distinct routes, subject to immutable delivery deadline.

## 3.12 Radio and ESP32 shared PHY

Radio accepts one complete immutable bounded logical transfer to one directly reachable Radio peer/handle. MaximumLogicalTransferSize is finite and distinct from physical MTU. Radio owns bounded fragmentation/reassembly and physical retry. Receive provides complete opaque logical transfer plus Radio-owned peer/address observations and qualified timing metadata. Radio never interprets DeviceIdentifier, MembershipIncarnation, MeshMessageId, PrimitiveFamilyId or Mesh routing/destination semantics. Precision timing uses a separate protected path.

Radio may create generation-safe process-local peer handles before authentication. Mesh binds these beside its incarnation-scoped RadioIdentifier; peer handles never become Mesh identity and are never distributed/persisted as identity.

ESP32 ordinary Wi-Fi and Raw80211 share one integrated PHY coordinated by ESPressio-ESP32. Raw follows effective Wi-Fi channel by default and must never disruptively retune an incompatible active service. Channel/tx-power/power-save global state belongs to the ESP32 coordinator; Mesh sees only link/topology availability changes.

## 3.13 Clock, traffic, execution, payload and policy signature

Clock roles are elective, not configured permanently. Timing owns math/discipline/quality; Radio owns direct timestamp mechanics; Mesh owns root/parent coordination. Election is quality-first with DeviceIdentifier final deterministic tie-break. Precision T1/T2/T3/T4 bypasses ordinary Mesh queues/fragmentation.

`IMeshTrafficGovernor` is injected. Default independently protected capacities: InfrastructureResponse=8, ClockControl=4, GeneralControl=8, Application=8. Application saturation cannot consume control reserves; mandatory delivery ACKs remain serviceable. `IControlWorkLifetimePolicy` gives control work finite lifetime; no control work may remain queued indefinitely.

Mesh decomposes into narrow services; responsibility does not imply a task. Prefer ESPressio Task/PrecisionThread; no direct FreeRTOS in Mesh. One serialized Mesh execution domain is an acceptable baseline. All queues/resources are finite and expose backpressure.

Payload forms: Borrowed Stable Payload / Bounded Owned Payload / Repeatable Serialized Source. Finite size must be known before acceptance; one MeshMessageId maps to immutable logical bytes across retries/routes/fragments. Selective multicast may share immutable backing.

Distributed-authority-changing policy/config contributes recursively to MeshSignature through stable identifiers/versions/configuration signatures/delegate contracts; never RTTI/compiler/layout/function addresses. Operational local mechanisms normally do not.

## 3.14 Locked default capacities / memory planning

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
- tombstone retention ~5 min

Provisional fixed/cardinality metadata ≈60,160 B plus starting task stacks ≈10,000 B = ≈70,160 B before variable payload/control/reassembly/runtime overhead.

`DRAM ≈ 70,160 + 20*Papp + 8*Pinfra + 4*Pclock + 8*Pgeneral + R + C + A + X`

Concrete logical/control/reassembly byte capacities remain finite and measured.

---

# 4. Current implementation state

## 4.1 Validated lower/platform libraries

- **System:** canonical exact-16-B DeviceIdentifier validated.
- **Primitive:** family/version/message/correlation/fingerprint vocabulary and central allocation boundaries validated; concrete family IDs still unallocated.
- **Radio:** bounded direct-link logical transfer, 4096-B generic ceiling, four reassembly slots, 32 recent completions. Compact 4-B generation-safe `RadioPeerHandle` and fixed-capacity `RadioPeerRegistry`; `RadioTransport` owns peer registry, emits SourcePeer on inbound complete transfer, supports preferred handle-based direct unicast, explicitly invalidates peers on explicit removal/interface removal/Stop, and exposes Observable peer lifecycle. Peer saturation never falls back to raw provider address as identity and does not poison retry suppression.
- **ESP32:** shared-PHY coordinator / Raw80211 coexistence validated.
- **NRF24:** direct-link semantics and bounded logical-transfer behavior validated.
- **State:** structural State migration complete/validated, including non-owning local binding, authoritative/effective availability split and non-reentrant serialized notifications.
- **Command:** intrinsic RPC/request-response/timeout/reply model removed; Primitive-backed bounded CommandMessage validated. `CommandResult` wording still needs local-disposition clarification only.
- **Event:** provenance/context separation and EVTT v2 validated; no HopCount/transport state in Event; distributed EventTypeId remains unresolved.

## 4.2 Mesh validated foundations

Implemented on `structural_realignment_propagation`:

- core identity/value types and locked limits.
- `MembershipTombstoneTable<64>`.
- `DeduplicationWindow<128>`.
- nonwrapping `MeshMessageIdGenerator`.
- `InboundDeliveryReservationTable<8>`.
- `AuthenticatedMembershipTable<32>` with source/incarnation-owned committed dedup.
- `InboundDeliveryCoordinator` composing authenticated membership → dedup → InProgress → definitive commit/retryable release.
- `MembershipRetentionCoordinator` and `MembershipLifecycleCoordinator`.
- injected `IMeshLivenessPolicy`, default liveness policy and `MembershipLivenessTracker<32>` with exact-incarnation evidence inspection.
- `LivenessProbeReservationTable<4>`, injected `IMeshLivenessProbePolicy`, wire-neutral `ILivenessProbeInitiator` and `LivenessProbeCoordinator`; an outstanding probe is never treated as authentication/evidence.
- `PendingNeighbourCandidateTable<8>` and `InboundAuthenticationReservationTable<4>`; unauthenticated claims remain outside authenticated member/dedup state.
- `AdmissionPromotionCoordinator`; only separately authenticated DeviceIdentifier + MembershipIncarnation may enter Validating membership.
- `PrimitiveReceiverRegistry<8>`: fixed-array, generation-safe, one receiver per family, bounded dispatch, support descriptor/version/fingerprint/exposure. Added bounded `ForEachDescriptor` enumeration that exposes semantic descriptors only, never receiver pointers.
- `PrimitiveSupportDescriptor` + `AdvertisedPrimitiveSupportSnapshot<8>`: fixed-capacity canonical snapshot of **Advertised** external family support. Hidden registrations remain locally dispatchable but are excluded. Snapshot copies family/version/fingerprint only, never registration handles/receiver pointers, and sorts by `PrimitiveFamilyId`, so equivalent active support sets produce deterministic authenticated-profile input independent of local registration order. The unresolved Mesh control family is intentionally omitted until its central family ID is allocated.
- `MeshRadioRegistry<4>`: process-local IRadio* ↔ incarnation-scoped non-recycled RadioIdentifier.
- `NeighbourDiscoveryCoordinator`: Radio SourcePeer + registered ingress Radio + untrusted claim → bounded pre-auth candidate.
- `RadioPeerLifecycleCoordinator`: peer invalidation releases matching pre-auth auth work/candidates only; authenticated membership is deliberately inaccessible.
- injected `IMeshTrafficGovernor` + protected default 8/4/8/8 governor.
- injected `IControlWorkLifetimePolicy` + fixed composition-root configuration + overflow-safe deadline helper.
- umbrella header exports all current foundations.

The primitive-support snapshot is **profile input only, not a complete NodeProfile**. Full NodeProfile remains blocked on the Group identifier contract. Do not invent a Group ID to complete it.

## 4.3 Validation record

- `33785734451`: Mesh types + tombstones + dedup passed.
- `33786121103`: message-ID generator passed.
- `33786446663`: InProgress reservations passed.
- `33786968405`: authenticated membership + inbound delivery passed.
- `33787189204`: membership retention passed.
- `33789240336`: policy-driven liveness passed.
- `33792457888`: membership lifecycle + pre-auth resources passed.
- `33792536582`: self-contained liveness-header correction passed.
- `33793062493`: initial Primitive receiver CI exposed only missing include path; no budget symptom.
- `33793151350`: corrected Primitive include path; Mesh suite passed.
- Radio `33793581249`: peer registry + transport/clock + ESP32 smoke passed.
- Mesh `33793950787`: peer-bound admission + authenticated promotion passed.
- Mesh `33794119177`: handoff reconciliation passed.
- Radio `33797082636`: RadioTransport peer-handle integration passed native + ESP32.
- Mesh `33797422944`: MeshRadioRegistry + peer-bound discovery passed.
- Mesh `33797806648`: protected traffic governor passed.
- Mesh `33798395545`: finite control-work lifetime passed.
- Radio `33798291517`: bounded peer enumeration passed native + ESP32.
- Radio `33799876449`: Observable peer lifecycle passed native + ESP32.
- Mesh `33800204015`: Radio-peer lifecycle cleanup passed.
- Mesh `33800502633`: bounded liveness-probe reservations passed.
- Mesh `33801088318`: dedicated policy-driven liveness-probe coordinator passed.
- Mesh `33801088364`: general suite after liveness-probe coordinator passed.
- Mesh `33801574428`: dedicated deterministic advertised primitive-support snapshot build/test passed.
- Mesh `33801574478`: general suite after primitive-support snapshot passed.
- **No Actions-budget symptom has occurred.**

## 4.4 Critical implementation boundaries

- Authenticated membership contains only already-authenticated records; pre-auth candidates cannot mutate authenticated dedup/liveness state.
- Liveness evidence must already be authenticated for the exact incarnation.
- Probe reservation only bounds execution; probe policy owns eligibility/cadence and probe initiation owns mechanics. An outstanding probe is not authentication.
- Pending candidate identity claims are untrusted hints; authenticated promotion receives separately established identity.
- Radio peer handles are process-local Radio-owned link bindings, never Mesh identity.
- Peer invalidation removes only state whose authority is the peer binding itself; it never removes authenticated membership.
- Traffic governor protects work classes independently; mandatory receipts/ACKs belong to InfrastructureResponse.
- Control work requires finite lifetime before queue admission; operational values are composition-root supplied.
- Primitive support snapshot includes advertised external families only and is deterministic by family ID. It deliberately omits unresolved Mesh control family ID.
- Full NodeProfile must not be completed until Group identifier semantics are established.
- No concrete ESPressio `PrimitiveFamilyId` and no distributed EventTypeId may be invented autonomously.

---

# 5. Immediate next work

1. Inspect current `ESPressio-Security` abstractions before adding Mesh admission/security composition; reuse existing crypto/security facilities and keep Mesh security authority/admission policy wire-neutral.
2. Continue only unambiguous authenticated-profile pieces; the deterministic primitive-support snapshot is complete, while full NodeProfile remains blocked by Group identifier semantics.
3. Reconcile Command `CommandResult` source/docs wording so `success` is unmistakably local handler/dispatch disposition, never requested-operation completion; avoid gratuitous public rename.
4. Keep Event distributed identity work paused pending Simon's explicit EventTypeId/message-ID decisions.
5. Continue topology → routing → traffic-governed bounded queues → clock coordination → adapters in dependency order.

---

# 6. Chronological UTC work log

- **2026-09-03T12:59:05Z** — Simon authorized implementation; created Mesh primary propagation branch, affected propagation branches and authoritative handoff.
- **2026-09-03T13:13:44Z** — System DeviceIdentifier and Primitive vocabulary/allocation boundaries implemented/validated; no concrete family IDs allocated.
- **2026-09-03T13:22:31Z** — ESP32/NRF24/State propagation created; initial Mesh types/limits added.
- **2026-09-03T13:47:30Z–14:03:32Z** — Radio direct-link realignment + ESP32 shared-PHY + NRF24 propagation validated.
- **2026-09-03T14:09:00Z–16:27:18Z** — State structural migration completed and validated.
- **2026-09-03T16:40:00Z–16:43:07Z** — Command RPC removal and bounded Primitive-backed CommandMessage validated.
- **2026-09-03T16:45:00Z–17:32:26Z** — Event provenance/context separation and EVTT v2 validated; distributed EventTypeId left unresolved.
- **2026-09-03T17:36:00Z–17:38:18Z** — Reachability corrected to Suspect; tombstones + dedup implemented.
- **2026-09-03T17:42:18Z–17:53:20Z** — Message-ID generator, InProgress reservations, authenticated membership, inbound delivery and retention foundations added/validated.
- **2026-09-03T18:12:00Z–18:13:58Z** — Policy-driven liveness tracker validated.
- **2026-09-03T18:44:00Z–18:47:20Z** — Membership lifecycle and bounded pre-auth resources validated.
- **2026-09-03T18:49:00Z–18:53:30Z** — PrimitiveReceiverRegistry added without family allocation; include-path issue corrected; suite passed.
- **2026-09-03T18:56:00Z–19:01:40Z** — RadioPeerHandle/registry, peer-bound candidates and authenticated promotion validated.
- **2026-09-03T19:31:00Z–19:37:00Z** — RadioTransport peer-handle integration, MeshRadioRegistry and peer-bound discovery validated.
- **2026-09-03T19:38:00Z–19:47:00Z** — Protected traffic governor, bounded peer enumeration and finite control-work lifetime validated.
- **2026-09-03T20:01:00Z–20:06:00Z** — Observable Radio peer lifecycle and Mesh pre-auth peer-loss convergence validated.
- **2026-09-03T20:06:00Z–20:15:00Z** — Bounded liveness-probe reservations and policy-driven/wire-neutral probe coordination validated (`33800502633`, `33801088318`, `33801088364`).
- **2026-09-03T20:17:00Z–20:20:00Z** — Added bounded descriptor enumeration and deterministic `AdvertisedPrimitiveSupportSnapshot`: advertised family/version/fingerprint only, Hidden excluded, family-ID sorted, no receiver pointers/handles, no Mesh family-ID allocation. Dedicated `33801574428` and general `33801574478` passed. Full NodeProfile remains blocked on Group identifier semantics.
