# ESPressio-Mesh Structural Realignment Tranche Handoff

Authoritative implementation handoff for the ESPressio-Mesh-origin structural-realignment tranche. Maintain this file on `ESPressio-Mesh/structural_realignment_propagation` after every meaningful implementation, validation, branch, dependency, architecture or correction change.

## Branch scope

Primary context repository:
- `ESPressio-Mesh`: `structural_realignment_propagation`

Affected repositories:
- `ESPressio-System`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Primitive`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Radio`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-ESP32`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-NRF24`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-State`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Command`: `structural_realignment_propagation_ESPressio-Mesh`
- `ESPressio-Event`: `structural_realignment_propagation_ESPressio-Mesh`

`ESPressio-MeshAdapters` propagation work remains deferred until adapter implementation starts.

## Mandatory tranche rules

- Every Markdown policy in `ESPressio-Development-Platform/Policy` is authoritative unless Simon explicitly states an exception.
- Ordinary libraries do not call hardware/platform APIs directly; use `ESPressio-System` abstractions. Platform/technology concretes belong in their platform repositories.
- Do not introduce circular dependencies, including optional ones. Stop and ask Simon if one would be required.
- All participating dependency references, tests, jobs and documentation must point to the matching propagation branches.
- No version-number changes merely as tranche work.
- Do not preserve obsolete APIs merely for compatibility.
- Documentation/comments evolve with code; prefer IDE-compatible doc comments.
- Logging uses `ESPressio-Logging` and must remain compile-switchable; optional callback behavior follows `ESPressio-Observable` patterns.
- If Actions fail immediately because the monthly task/job budget is exhausted, record that fact and switch to recursive self-review rather than treating CI as evidence.
- The repository is `ESPressio-Primitive` singular.

## Frozen architecture

Layering:
- System: foundational platform-neutral identities/services; canonical `DeviceIdentifier`.
- Primitive: common conceptual-message vocabulary only.
- Command/Event/State: semantic primitive libraries.
- Radio: opaque direct-link transport and link-local mechanics.
- Mesh: membership, topology, routing, admission, multi-radio forwarding, logical addressing and end-to-end delivery over Radio. Mesh must not depend on Command/Event/State.
- MeshAdapters: optional integration above Mesh and semantic primitive libraries.
- Timing: clock math/discipline/quality; Mesh coordinates root/parent; Radio owns direct timestamp mechanics.

Identity and bounds:
- `DeviceIdentifier`: exact 16 bytes, zero invalid, permanent and transport-independent.
- `MeshIdentifier`: exact nonzero 16 bytes.
- `MembershipIncarnation`: exact nonzero 16 bytes.
- `MeshNodeAlias`: uint16 Mesh-local routing handle.
- `RadioIdentifier`: uint8; 0 invalid, 1-254 usable, 255 reserved.
- `MeshMessageId`: uint64 nonzero monotonic per authenticated source DeviceIdentifier+MembershipIncarnation; never wraps/reuses. One independent Node delivery gets one ID; each selective-multicast recipient gets one ID; Broadcast keeps one ID through the flood.
- `ProfileGeneration`/`TopologyGeneration`: uint64 nonzero.
- `CanonicalName`: mandatory Mesh-unique NodeProfile property, 1-32 printable ASCII bytes, exact case-sensitive comparison.

Primitive:
- `PrimitiveFamilyId`: 0 invalid; 0x0001-0x7FFF ESPressio-defined; 0x8000-0xFFFE application/private; 0xFFFF reserved.
- `ContractFingerprint`: exact 32-byte SHA-256 semantic compatibility digest, not authentication.
- Family version ranges negotiate highest mutual version.
- Support, Compatible, Reachable and Authorized are distinct concepts.
- Concrete ESPressio family IDs remain deliberately unallocated. Do not invent one.

Destinations/delivery:
- API destinations: Node, Group, CapabilitySelector, Broadcast.
- Group/CapabilitySelector resolve once at sender into a frozen bounded recipient set, then become independent Node deliveries.
- Selective multicast owns one sender-local aggregate handle/deadline and immutable shared payload backing, but independent recipient MessageIds/outcomes/receipts. No wire MulticastOperationId.
- Broadcast is best-effort controlled flooding with no frozen recipient set, destination ACK or complete-to-all promise.
- Delivery ACK means authenticated destination acceptance only; it is not command completion or business-operation success.

Membership/security:
- Admission states: Unknown -> Discovered -> Authenticating -> Validating -> Joining -> Active.
- Cheap Mesh/protocol/signature checks precede crypto.
- Any Active member may sponsor a candidate; sponsor is not leader.
- MembershipState and ReachabilityState are separate.
- Authenticated Leave is authoritative; FastRejoin never replaces authentication.
- Security/admission behavior is injected through `IMeshSecurityAuthority`/`IMeshAdmissionPolicy`.
- Reachability hysteresis: Reachable -> Suspect -> Unreachable. Default policy floors approximately 5 s and 15 s; authenticated evidence may immediately restore Reachable.
- Full unreachable record retention default 60 s; compact membership tombstones default approximately 5 min.

Routing/topology:
- Directed link-state graph, global `MaxTopologyLinks=96`, route cache 32, `MaxRouteHops=16`, `DefaultHopLimit=16`.
- Same-route maximum attempts 3; distinct-route maximum 4.
- Route cache is disposable local optimization, never authority.
- Routing is adaptive hop-by-hop; no mandatory source routing.
- RemainingHopLimit decrements only on a proven successful Mesh forwarding transition, never merely because Radio accepted a submission.
- Routing strategy is injected and no universal scalar RouteCost has been imposed.

Radio:
- Public abstraction accepts a complete immutable bounded logical transfer for one directly reachable Radio peer.
- Radio owns hop-local fragmentation/reassembly/physical retries and Radio-owned peer identity.
- `RadioPeerHandle` is generation-safe and process-local; Mesh never falls back to raw Radio addresses for authenticated forwarding.
- Immediate `Send` acceptance is admission only. Direct-link terminal evidence is separate and may distinguish transmission completion from peer acknowledgement.
- Precision timing exchange remains a separate protected Radio/Timing path.
- ESP32 Wi-Fi and Raw80211 share one platform-level PHY coordinator; incompatible active services produce restriction/unavailability, never disruption.

Clock:
- Every eligible node is elective; no configured fixed clock roles.
- Injectable quality, eligibility, root-election and parent-selection policies.
- Quality-first deterministic DeviceIdentifier tie-break.
- Precision T1/T2/T3/T4 bypass normal Mesh queues/fragmentation.
- Same-root and strictly-better-stratum rules prevent loops.

Traffic/execution/payload:
- `IMeshTrafficGovernor` injected. Default protected capacities: Infrastructure Responses 8, Clock Control 4, General Control 8, Application 8.
- Mandatory delivery ACKs remain serviceable under application saturation.
- Control work has finite injected lifetime policy.
- Single-owner mutable state; one serialized Mesh execution domain is acceptable. Prefer ESPressio Task/PrecisionThread, never direct FreeRTOS in Mesh.
- Payload models: Borrowed Stable Payload, Bounded Owned Payload, Repeatable Serialized Source. Finite length is known before acceptance; one MessageId maps to immutable logical bytes through retries/routes/fragments.

Locked capacities:
`MaxMeshNodes=32`, `MaxRadiosPerNode=4`, `MaxGroupsPerNode=8`, `MaxPrimitiveReceivers=8`, `MaxActiveApplicationTransmissions=8`, `MaxTopologyLinks=96`, `MaxRouteHops=16`, `DefaultHopLimit=16`, route cache 32, `MaxActiveInboundDeliveries=8`, `MaxPendingNeighbourCandidates=8`, `MaxActiveLivenessProbes=4`, `MaxActiveInboundAuthentications=4`, `MaxRecipientsPerTransmission=32`, `MaxMembershipTombstones=64`, dedup 128, same-route attempts 3, routes 4.

## Implemented tranche state

System:
- Canonical exact-16-byte DeviceIdentifier implemented; ESP32-specific derivation remains outside System.

Primitive:
- PrimitiveFamilyId, PrimitiveProtocolVersion, ConceptualMessageId, CorrelationId, inclusive protocol range/highest-mutual negotiation, exact 32-byte ContractFingerprint and registry boundaries implemented. No concrete family ID allocated.

Radio/ESP32/NRF24:
- Radio realigned to direct-peer complete logical transfer.
- Generation-safe `RadioPeerHandle`/bounded peer registry implemented.
- RadioTransport owns bounded fragmentation/reassembly and peer lifecycle.
- Direct-link evidence separates submission/admission from actual terminal transmission/peer-ack evidence.
- Deferred logical-transfer correlation is explicitly bounded and reserved before partial submission; saturation returns ResourceUnavailable before any fragment is sent.
- ESP32 Raw80211/BLE and NRF24 propagation implementations remain within the Radio abstraction; shared ESP32 PHY coordination is implemented.

State:
- Canonical StateAddress, local non-owning authority binding, remote replicas, availability/reachability separation, epoch/revision behavior, observer serialization/reentrancy protection and v2 transport primitives implemented and validated.

Command:
- Intrinsic RPC request/response, pending-response and timeout machinery removed; `CommandMessage` is Primitive-backed conceptual/correlation/version + bounded payload. Remaining cleanup: rename/reframe local `CommandResult` and completion-oriented wording so local success means framework acceptance only.

Event:
- Transport provenance removed from Event object state; dispatch context carries Local/Remote origin; remote Events are not auto-forwarded; EVTT v2 removes HopCount semantics. Full intrinsic Event identity integration remains blocked on canonical distributed EventTypeId allocation/derivation and Event ConceptualMessageId generation strategy.

Mesh:
- Membership tombstones, deduplication window, MeshMessageId generator, inbound reservations/coordinator, authenticated membership, retention/liveness/lifecycle composition, bounded pre-auth admission resources and security/admission boundaries implemented.
- Primitive receiver registry/support snapshot, neighbour discovery, Mesh Radio registry, peer lifecycle and authenticated direct-peer bindings implemented.
- Directed topology snapshot/freshness/publication/graph storage, route/cache, injected routing strategy/planner, bounded route-attempt/retry policies and forwarding transition/submission lifecycle implemented.
- Radio terminal correlation and forwarding attempt composition preserve admission-vs-terminal evidence separation.
- Delivery acknowledgement tracking/coordinator and outbound delivery lifecycle are implemented without conflating ACK with application completion.
- Clock coordination, membership/reachability integration and parent usability foundations are implemented.
- Target-native fixed/cardinality memory accounting exists and intentionally uses `sizeof` rather than presenting host ABI numbers as ESP32 measurements.
- `ApplicationTransmissionTable<8,32>` owns bounded sender-local selective application transmission aggregates. Each accepted aggregate has one generation-safe handle and immutable absolute deadline, a frozen set of at most 32 unique recipient Devices/incarnations, one independent nonzero MeshMessageId per recipient, and independent terminal outcome. It owns no payload bytes, routes, Radio state, ACK tracker, selector identity or Broadcast semantics. Capacity exhaustion is explicit. Expiry marks only still-pending recipients. Slot reuse advances generation so stale handles cannot mutate replacements.
- `ApplicationTransmissionCoordinator<8,32>` now composes exactly one `IMeshTrafficGovernor` Application reservation with each accepted aggregate and can begin independently supplied per-recipient `OutboundDeliveryLifecycle`s using the aggregate's frozen identity, MessageId and deadline. It deliberately does not allocate a hidden 8*32 active-delivery pool. Invalid/table-capacity failures roll back traffic admission; the Application reservation remains held while any recipient is pending and is released when the aggregate becomes terminal or is explicitly released. Aggregate results may remain queryable after governance capacity is released.
- Application transmission metadata is included in target-native fixed/cardinality memory accounting; immutable shared payload backing remains a separate variable-capacity term.

## Validation status

Representative green tranche runs before the latest change:
- Radio native + ESP32 worker after deferred logical-transfer correlation: `33956249777` green.
- Mesh forwarding Radio-attempt composition: Topology Evolution `33959946510` green.
- Mesh outbound Radio delivery correlation: Topology Evolution `33960956661` green.
- General CI before application-transmission work: `33961248712` green.
- Memory Accounting before application-transmission work: `33961248739` green.
- Clock Coordination before application-transmission work: `33961248717` green.

Application-transmission continuation:
- Initial Topology Evolution `33963070378` reached the new table test and exposed only a test namespace typo (`System` vs `ESPressio::System`); all preceding routing/delivery regressions were green. The typo was corrected in commit `94398a0a19ee5d2ea4619b26befae09dcb832236`.
- Follow-up Topology Evolution `33963104967`: all topology, routing, forwarding, acknowledgement, application-transmission, security-authority and umbrella-build steps completed successfully.
- Application-transmission metadata accounting was added and validated by the subsequent memory-accounting commit sequence.
- Application lifecycle composition Topology Evolution `33963873242`: all topology/routing/forwarding/ACK/application aggregate tests, the new application coordinator regression, security-authority regression and current Mesh umbrella build completed successfully.
- At this handoff write, other workflows triggered by the final workflow commit (Memory Accounting `33963873147`, Topology Freshness `33963873196`, Control Work Lifetime `33963873213`, Clock Coordination `33963873194`, CI `33963873269`) were queued/running; do not claim them green until checked.

## Unresolved public/wire decisions — do not cross silently

1. Concrete ESPressio-defined `PrimitiveFamilyId` allocations.
2. Canonical distributed EventTypeId derivation/allocation.
3. Event ConceptualMessageId generation scope/strategy.
4. Group identity/wire representation when Group implementation becomes unavoidable.
5. Concrete Mesh security handshake/profile/wire schema.
6. Final concrete ordinary/control/reassembly byte capacities required for a whole-device DRAM budget.

## Immediate next work

1. Confirm the remaining workflows triggered by application lifecycle composition.
2. Continue sender-side application composition toward immutable shared payload ownership/source semantics without duplicating payload bytes per recipient; do not choose final payload byte capacities without explicit approval.
3. Perform Command local-dispatch terminology cleanup when practical; do not preserve misleading `CommandResult` semantics for compatibility.
4. Continue memory accounting as new fixed stores are introduced; keep variable payload/reassembly/security-provider storage separate.
5. Do not begin Event/MeshAdapters wire integration until the unresolved ID decisions are explicitly approved.

## Chronological continuation log

- 2026-09-05T08:42Z-08:50Z: Integrated explicit-capacity deferred logical-transfer correlation into RadioTransport. Added pre-fragment reservation, per-fragment registration, interface observer subscriptions, explicit ResourceUnavailable before partial submission, peer-handle retention in terminal descriptor, synchronous/deferred terminal emission, and saturation/completion/failure/stale-handle regression. Radio run `33956249777` green native + ESP32 worker.
- 2026-09-05T09:17Z-09:20Z: Added Mesh `ForwardingRadioTerminalCorrelation` and responsibility-focused regression.
- 2026-09-05T10:07Z-10:11Z: Added `ForwardingRadioAttemptCoordinator`; saturation is pre-submission, accepted Send binds deferred handles, later terminal evidence feeds forwarding lifecycle. Topology Evolution `33959946510` green.
- 2026-09-05T10:29Z-10:33Z: Added authenticated NextHop/Incarnation to accepted forwarding result and `OutboundRadioDeliveryCoordinator`; acceptance-before-Radio-terminal, terminal failure retry and cancellation cleanup covered. Topology Evolution `33960956661` green.
- 2026-09-05T11:16Z-11:20Z: Added bounded `ApplicationTransmissionTable<8,32>`, regression, umbrella export and Topology Evolution coverage. Initial run `33963070378` exposed only a test namespace typo after all prior steps passed.
- 2026-09-05T11:21Z: Corrected the test namespace in `94398a0a19ee5d2ea4619b26befae09dcb832236`; follow-up Topology Evolution `33963104967` completed every regression and umbrella build successfully.
- 2026-09-05T11:23Z: Added application-transmission metadata to `MeshFixedMemoryAccounting` and updated its regression. Immutable shared payload backing remains intentionally outside fixed/cardinality accounting.
- 2026-09-05T11:35Z-11:39Z: Added `ApplicationTransmissionCoordinator<8,32>` to bind one Application traffic-governor reservation to one accepted aggregate while beginning externally owned per-recipient `OutboundDeliveryLifecycle`s from frozen recipient identity/MessageId/deadline. No hidden 256-delivery allocation was introduced. Added rollback/terminal/expiry/saturation regression, umbrella export and Topology Evolution coverage. Run `33963873242` completed successfully.