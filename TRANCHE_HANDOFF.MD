# ESPressio-Mesh Structural Realignment Tranche Handoff

Authoritative implementation handoff for the ESPressio-Mesh-origin structural-realignment tranche. Maintain this file on `ESPressio-Mesh/structural_realignment_propagation` after every meaningful implementation, validation, branch, dependency, architecture or correction change.

## Branch scope

Primary: `ESPressio-Mesh`: `structural_realignment_propagation`.
Affected: System, Primitive, Radio, ESP32, NRF24, State, Command and Event on `structural_realignment_propagation_ESPressio-Mesh`. `ESPressio-MeshAdapters` remains deferred until adapter implementation starts.

## Mandatory tranche rules

- Every Markdown policy in `ESPressio-Development-Platform/Policy` is authoritative unless Simon explicitly states an exception.
- Ordinary libraries do not call hardware/platform APIs directly; platform concretes belong in platform/technology repositories.
- No circular dependencies, including optional ones. Stop and ask Simon if one would be required.
- Participating dependency references/tests/jobs/docs point to matching propagation branches.
- No tranche-driven version-number changes and no obsolete API preservation merely for compatibility.
- Documentation/comments evolve with code; logging uses ESPressio-Logging and optional callback behavior follows ESPressio-Observable.
- If Actions fail immediately from monthly task/job exhaustion, record it and switch to recursive self-review.
- Repository name is `ESPressio-Primitive` singular.

## Frozen architecture

Layering: System foundations; Primitive common conceptual-message vocabulary; Command/Event/State semantic primitives; Radio opaque direct-link transport; Mesh membership/topology/routing/admission/multi-radio forwarding/logical addressing/end-to-end delivery; MeshAdapters optional integration above Mesh and semantic libraries; Timing owns clock math while Mesh coordinates root/parent and Radio owns direct timestamp mechanics.

Identity: DeviceIdentifier exact 16 B zero-invalid; MeshIdentifier and MembershipIncarnation exact nonzero 16 B; MeshNodeAlias uint16 Mesh-local; RadioIdentifier uint8 (0 invalid, 1-254 usable, 255 reserved); MeshMessageId uint64 nonzero monotonic per authenticated source DeviceIdentifier+MembershipIncarnation and never reused; ProfileGeneration/TopologyGeneration uint64 nonzero; CanonicalName mandatory Mesh-unique 1-32 printable ASCII bytes.

PrimitiveFamilyId ranges remain frozen; concrete ESPressio family IDs remain deliberately unallocated. ContractFingerprint is exact 32-byte SHA-256 semantic compatibility digest. Support/Compatible/Reachable/Authorized remain distinct.

Destinations: Node, Group, CapabilitySelector, Broadcast. Group/CapabilitySelector resolve once into a bounded frozen recipient set and then independent Node deliveries. Selective multicast has one sender-local aggregate/deadline/shared immutable payload but independent recipient MessageIds/outcomes/receipts; no wire MulticastOperationId. Broadcast remains best-effort controlled flooding with no complete-to-all promise. Delivery ACK means authenticated destination acceptance only.

Membership: Unknown→Discovered→Authenticating→Validating→Joining→Active. Membership and reachability separate. FastRejoin never replaces authentication. Reachability baseline Reachable→Suspect→Unreachable at approximately 5s/15s; authenticated evidence may immediately restore Reachable. Full unreachable retention default 60s, compact tombstones approximately 5min.

Routing: global MaxTopologyLinks96, route cache32, MaxRouteHops16, DefaultHopLimit16, same-route attempts3, distinct routes4. Adaptive hop-by-hop; no mandatory source routing or universal scalar RouteCost. RemainingHopLimit decrements only on proven successful Mesh forwarding transition.

Radio: complete immutable bounded logical transfer to directly reachable Radio peer; Radio owns fragmentation/reassembly/physical retries and peer identity. RadioPeerHandle is generation-safe/process-local. Send acceptance is admission only; terminal evidence distinguishes transmission completion and peer acknowledgement where supported. Deferred provider evidence uses provider-local generation/correlation handles and bounded composition-supplied logical-transfer tracking. Precision timing remains a separate protected path.

Traffic/execution: injected IMeshTrafficGovernor with protected capacities Infrastructure8, Clock4, General8, Application8. Control work finite lifetime. Serialized Mesh mutable-state domain acceptable; no direct FreeRTOS in Mesh.

Payload: Borrowed Stable Payload, Bounded Owned Payload, Repeatable Serialized Source. Finite length known before acceptance. One MessageId maps to immutable logical bytes through retries/routes/fragments. Selective multicast recipients share one payload backing. Bounded-owned byte capacity remains deliberately unresolved. Repeatable Serialized Source may use composition-supplied finite `IApplicationPayloadStagingBuffer`; Mesh defines no default staging capacity.

Locked capacities: MaxMeshNodes32, MaxRadiosPerNode4, MaxGroupsPerNode8, MaxPrimitiveReceivers8, MaxActiveApplicationTransmissions8, MaxTopologyLinks96, MaxRouteHops16, DefaultHopLimit16, route cache32, MaxActiveInboundDeliveries8, MaxPendingNeighbourCandidates8, MaxActiveLivenessProbes4, MaxActiveInboundAuthentications4, MaxRecipientsPerTransmission32, MaxMembershipTombstones64, dedup128, same-route attempts3, routes4.

## Implemented tranche state

- System: canonical exact-16-byte DeviceIdentifier implemented; platform derivation outside System.
- Primitive: family/version/message/correlation/fingerprint/range contracts implemented; no concrete family ID allocated.
- Radio/ESP32/NRF24: direct-peer complete logical transfer, bounded peer registry, fragmentation/reassembly, peer lifecycle, explicit send-attempt vs terminal transmission evidence, bounded deferred logical-transfer correlation, ESP32 shared-PHY coordination, and synchronous qualified NRF24 completion/ack evidence. Raw80211 and BLE do not fabricate stronger terminal evidence than their providers establish.
- State: canonical StateAddress, local authority binding, remote replicas, availability/reachability, epoch/revision, serialized observers and v2 transport primitives.
- Command: intrinsic RPC machinery removed; CommandMessage is conceptual/correlation/version + bounded payload. CommandResult is local invocation disposition only; it does not mean requested application work completed.
- Event: transport provenance removed from Event object state; Local/Remote dispatch context and EVTT v2 implemented. Distributed EventTypeId and conceptual MessageId strategy remain unresolved.

Mesh foundations implemented:
- Membership tombstones/dedup/message IDs/inbound coordination/authenticated membership/retention/liveness/lifecycle/pre-auth admission/security boundaries.
- Primitive receiver/support snapshot, neighbour discovery, Mesh Radio registry, peer lifecycle and authenticated direct-peer bindings.
- Directed topology/freshness/publication/graph, route/cache, injected routing/planner, bounded attempt/retry and forwarding submission/transition.
- Radio terminal correlation, delivery ACK tracking/coordinator and outbound delivery lifecycle without conflating admission, link evidence, destination acceptance or application completion.
- Clock coordination/membership/reachability/parent-usability foundations.
- Target-native fixed/cardinality memory accounting using sizeof; variable payload/reassembly/security-provider storage remains separate.

Application delivery implementation:
- `ApplicationTransmissionTable<8,32>` owns bounded selective aggregates with generation-safe handle, immutable deadline, frozen unique recipients, independent MessageIds/outcomes and exactly one immutable `ApplicationPayload` reference shared by all recipients. It owns no variable-capacity payload bytes, routes, Radio state, ACK tracker, selector identity or Broadcast semantics.
- `ApplicationTransmissionCoordinator<8,32>` composes one Application traffic reservation, aggregate/shared payload and externally supplied per-recipient `OutboundDeliveryLifecycle`. `ExpireDue(now)` terminalizes pending recipients at the immutable deadline and releases newly terminal aggregates' traffic reservations without releasing aggregate records/payload references. `BeginRecipient` records DeadlineExpired immediately when necessary. `Release` refuses non-terminal aggregates and is cleanup, never implicit cancellation. Exact-recipient outcome lookup supports reconciliation with external delivery state.
- `ApplicationRecipientLifecycleCoordinator` commits aggregate terminal outcome before retiring the exact matching external delivery. `RetireTerminal` handles recipients terminalized independently, especially deadline sweeping. `Inspect` now provides a non-mutating aggregate-authority preflight for composition layers. If a deadline sweep or another owner wins immediately before a late terminal result, `Terminalize` preserves the already-committed aggregate outcome but still resets the exact matching stale delivery lifecycle, releasing pending ACK/forwarding state. A late Delivered result therefore cannot overwrite DeadlineExpired and cannot leak external delivery state.
- `ApplicationDeliveryAcknowledgementCoordinator` is the application-aggregate boundary for authenticated destination ACKs. It preflights the aggregate/message pair before touching per-delivery ACK state; unknown transmissions/recipients therefore cannot consume an ACK. Already-terminal aggregate state wins without applying the late ACK: the exact stale delivery lifecycle is retired through `RetireTerminal`. Only a known Pending recipient may consume the authenticated ACK, after which Delivered or DeadlineExpired is committed to the aggregate and the exact delivery lifecycle is retired. Delivery ACK remains destination-framework acceptance only, never application-operation completion or Radio/link evidence.
- `IApplicationPayloadStagingBuffer` is injected composition-owned finite scratch storage with no Mesh-default byte capacity. `ApplicationForwardingCoordinator` binds one frozen recipient to immutable payload and authenticated forwarding submission. Borrowed Stable payload avoids application/per-recipient copying; Repeatable Serialized Source uses injected staging; RadioTransport owns physical fragmentation.
- `CachedRoutingCoordinator` composes RouteCache and RoutePlanner. Cached routes are reused only after structural, topology-generation and executable-direct-peer revalidation; invalid entries are removed and replanned. No cache eviction policy, source-routing semantics or universal RouteCost is invented.

During the 2026-09-05 continuation a tentative `ForwardingTerminalEvidence` bridge and peer-handle lookup were briefly introduced from an older state assumption. Recursive live-tip review immediately found existing `ForwardingRadioTerminalCorrelation` / `ForwardingRadioAttemptCoordinator` already own that responsibility. The duplicate file was deleted and the redundant direct-peer lookup reverted before further composition work. Do not reintroduce that duplicate abstraction.

## Validation status

Representative green runs: Radio deferred correlation `33956249777`; forwarding Radio-attempt `33959946510`; outbound Radio delivery `33960956661`; General CI `33961248712`; Memory Accounting `33961248739`; Clock Coordination `33961248717`; application aggregate `33963104967`; application lifecycle `33963873242`; immutable payload Topology Evolution `33964394229`; Application Forwarding `33964786992`; repeatable staging Application Forwarding `33965509782` and full CI `33965509790`; application deadline-sweep General CI `33966417039`; deadline-sweep Memory Accounting `33966417023`; Command corrective restoration `33966521660`; immediate-deadline General CI `33966745136`; immediate-deadline Memory Accounting `33966745105`; aggregate-retention full Mesh CI `33966912471`; Application Transmission/Memory Accounting `33966912509`; current continuation General Mesh CI `33967634010`; corrected deadline-sweep Application Forwarding `33968429741`; Memory Accounting `33968429720`.

Terminalization-race regression `6d0aeb685284967eedcea69c427fd20d255c4ffc` was validated by Application Forwarding `33968887276`.

Initial direct application-ACK composition at `e1b35dcea647a509e4cf2fb459c185d07b064123` is validated by Application Forwarding **`33969861973`** and full Mesh CI **`33969861977`**, both green. The subsequent preflight hardening is commits `e1a5501b4be396278c257ee9ba40b9a5fa1122f3`, `bbebe09389e5f6db05589c19c6b73b0930aaa854`, and regression `9fc0c460bcde498c7aac0a2686df5cf01d025490`; its follow-up Actions were started at 2026-09-05T13:49Z and must be checked before claiming that final hardening green. No Actions-budget symptom has occurred.

## Unresolved public/wire decisions — do not cross silently

1. Concrete ESPressio-defined PrimitiveFamilyId allocations.
2. Canonical distributed EventTypeId derivation/allocation.
3. Event ConceptualMessageId generation scope/strategy.
4. Group identity/wire representation when Group implementation becomes unavoidable.
5. Concrete Mesh security handshake/profile/wire schema.
6. Final concrete ordinary/control/reassembly and bounded-owned payload byte capacities for whole-device DRAM budgeting.

## Immediate next work

1. Validate the destination-ACK aggregate-authority preflight regression at `9fc0c460bcde498c7aac0a2686df5cf01d025490`; fix any failure before moving onward.
2. Continue recursive application-delivery lifecycle review, preserving the rule that no independently-owned terminal evidence may mutate/release its local state before the authoritative aggregate/message pair is known and reconcilable.
3. Preserve one frozen recipient = one independent MeshMessageId/delivery lifecycle while retaining exactly one shared immutable aggregate payload until every recipient is terminal.
4. Keep destination ACK distinct from Radio/link evidence and preserve immutable absolute delivery deadlines across all retries/routes.
5. Do not reinterpret next-hop acceptance as final destination delivery or invent completion semantics for unacknowledged multi-hop traffic.
6. Keep bounded-owned payload storage capacity unresolved until explicitly approved; do not infer staging capacity as the platform-wide owned-payload capacity.
7. Do not begin Event/MeshAdapters wire integration until unresolved ID decisions are approved.

## Chronological continuation log

- 2026-09-05T08:42Z-10:33Z: Integrated explicit-capacity deferred logical-transfer correlation into RadioTransport and Mesh forwarding terminal correlation/attempt/outbound-delivery composition. Representative runs `33956249777`, `33959946510`, `33960956661` green.
- 2026-09-05T11:16Z-12:00Z: Added bounded ApplicationTransmissionTable/Coordinator, immutable ApplicationPayload and ApplicationForwardingCoordinator. Runs `33963104967`, `33963873242`, `33964394229`, `33964786992` green.
- 2026-09-05T12:08Z-12:16Z: Reconciled live RoutePlanner/RouteAttemptCoordinator state, removed redundant tentative RoutingCoordinator, and added composition-supplied repeatable payload staging. Application Forwarding `33965509782` and full CI `33965509790` green.
- 2026-09-05T12:27Z-12:45Z: Added Command local-disposition regression, aggregate deadline sweeping, immediate-deadline consistency and terminal-only aggregate Release. Corrected a transient incomplete Command header reconstruction. Representative runs through full Mesh CI `33966912471` green.
- 2026-09-05T13:xxZ: Added ApplicationRecipientLifecycleCoordinator, exact outcome lookup, cache-safe CachedRoutingCoordinator and explicit external-lifecycle retirement after deadline sweep. Removed a redundant RoutingCoordinator reintroduced from an older snapshot. Corrected a stale test assertion about `BeginRecipient(nowMilliseconds)`. Application Forwarding `33968429741` and Memory Accounting `33968429720` green.
- 2026-09-05T13:26Z: Recursive lifecycle review found a race: if aggregate terminalization occurred between an external delivery result and `Terminalize`, the existing `AlreadyTerminal` path preserved aggregate state correctly but left the exact external delivery lifecycle active. Fixed in `68e0a4d33698daddf4cca593e230c93b6e539848`; regression `6d0aeb685284967eedcea69c427fd20d255c4ffc` proves DeadlineExpired remains authoritative while stale ACK-bearing delivery state is retired. Application Forwarding `33968887276` green.
- 2026-09-05T13:40Z-13:46Z: Reconciled Radio completion-evidence assumptions against the live tip. Confirmed current Radio already distinguishes send attempts from terminal transmission evidence and already has bounded deferred logical-transfer correlation. Removed a tentative duplicate Mesh terminal-evidence bridge and redundant peer lookup. Added `ApplicationDeliveryAcknowledgementCoordinator` so authenticated destination ACK consumption and aggregate recipient terminalization cannot be performed as separate caller steps. Initial Application Forwarding `33969861973` and full CI `33969861977` green.
- 2026-09-05T13:47Z-13:49Z: Recursive review found one remaining divergence edge: a caller could present a valid active delivery with an unknown aggregate handle, allowing ACK state to be consumed before aggregate terminalization failed. Added non-mutating `ApplicationRecipientLifecycleCoordinator::Inspect`, preflighted aggregate authority before ACK consumption, and changed already-terminal handling to retire stale delivery without applying the late ACK. Regression `9fc0c460bcde498c7aac0a2686df5cf01d025490` proves a stale aggregate handle cannot consume the pending ACK and the same ACK remains usable with the authoritative handle; follow-up CI pending at handoff update time.
