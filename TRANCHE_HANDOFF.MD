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

Membership: Unknown→Discovered→Authenticating→Validating→Joining→Active. Membership and reachability separate. FastRejoin never replaces authentication. Reachability policy baseline Reachable→Suspect→Unreachable at approximately 5s/15s; authenticated evidence may immediately restore Reachable. Full unreachable retention default 60s, compact tombstones approximately 5min.

Routing: global MaxTopologyLinks96, route cache32, MaxRouteHops16, DefaultHopLimit16, same-route attempts3, distinct routes4. Adaptive hop-by-hop; no mandatory source routing or universal scalar RouteCost. RemainingHopLimit decrements only on proven successful Mesh forwarding transition.

Radio: complete immutable bounded logical transfer to directly reachable Radio peer; Radio owns fragmentation/reassembly/physical retries and peer identity. RadioPeerHandle generation-safe/process-local. Send acceptance is admission only; terminal evidence distinguishes transmission completion and peer acknowledgement where supported. Deferred provider evidence uses provider-local generation/correlation handles and bounded composition-supplied logical-transfer tracking. Precision timing remains a separate protected path.

Traffic/execution: injected IMeshTrafficGovernor with protected capacities Infrastructure8, Clock4, General8, Application8. Control work finite lifetime. Serialized Mesh mutable-state domain acceptable; no direct FreeRTOS in Mesh.

Payload models: Borrowed Stable Payload, Bounded Owned Payload, Repeatable Serialized Source. Finite length known before acceptance. One MessageId maps to immutable logical bytes through retries/routes/fragments. Selective multicast recipients share one payload backing. Bounded-owned byte capacity remains deliberately unresolved and must not be invented. Repeatable Serialized Source forwarding may use a composition-supplied finite `IApplicationPayloadStagingBuffer`; Mesh defines no default staging capacity.

Locked capacities: MaxMeshNodes32, MaxRadiosPerNode4, MaxGroupsPerNode8, MaxPrimitiveReceivers8, MaxActiveApplicationTransmissions8, MaxTopologyLinks96, MaxRouteHops16, DefaultHopLimit16, route cache32, MaxActiveInboundDeliveries8, MaxPendingNeighbourCandidates8, MaxActiveLivenessProbes4, MaxActiveInboundAuthentications4, MaxRecipientsPerTransmission32, MaxMembershipTombstones64, dedup128, same-route attempts3, routes4.

## Implemented tranche state

System: canonical exact-16-byte DeviceIdentifier implemented; platform derivation outside System.
Primitive: family/version/message/correlation/fingerprint/range contracts implemented; no concrete family ID allocated.
Radio/ESP32/NRF24: direct-peer complete logical transfer, bounded peer registry, fragmentation/reassembly, peer lifecycle, explicit send-attempt vs terminal transmission evidence, bounded deferred logical-transfer correlation, ESP32 shared-PHY coordination, and synchronous qualified NRF24 completion/ack evidence implemented. Raw80211 and BLE do not fabricate stronger terminal evidence than their providers can establish.
State: canonical StateAddress, local authority binding, remote replicas, availability/reachability, epoch/revision, serialized observers and v2 transport primitives implemented.
Command: intrinsic RPC machinery removed; CommandMessage is conceptual/correlation/version + bounded payload. README/wiki/core comments define `CommandResult` as local invocation disposition only. Regression proves `Ok()` may coexist with an application operation that remains incomplete. Corrective restoration run `33966521660` is green.
Event: transport provenance removed from Event object state; Local/Remote dispatch context and EVTT v2 implemented. Distributed EventTypeId and conceptual MessageId strategy remain unresolved.

Mesh:
- Membership tombstones/dedup/message IDs/inbound coordination/authenticated membership/retention/liveness/lifecycle/pre-auth admission/security boundaries implemented.
- Primitive receiver/support snapshot, neighbour discovery, Mesh Radio registry, peer lifecycle and authenticated direct-peer bindings implemented.
- Directed topology/freshness/publication/graph, route/cache, injected routing/planner, bounded attempt/retry and forwarding submission/transition implemented.
- Radio terminal correlation, delivery ACK tracking/coordinator and outbound delivery lifecycle implemented without conflating admission, link evidence, destination acceptance or application completion.
- Clock coordination/membership/reachability/parent-usability foundations implemented.
- Target-native fixed/cardinality memory accounting uses sizeof; variable payload/reassembly/security-provider storage remains separate.
- `ApplicationTransmissionTable<8,32>` owns bounded selective aggregates with generation-safe handle, immutable deadline, frozen unique recipients, independent MessageIds/outcomes and exactly one immutable `ApplicationPayload` reference shared by all recipients. It owns no variable-capacity payload bytes, routes, Radio state, ACK tracker, selector identity or Broadcast semantics.
- `ApplicationPayload` represents Borrowed Stable bytes or a non-owning `IRepeatableSerializedPayloadSource`; both expose finite size before admission and immutable repeatable bytes for the aggregate lifetime. No payload byte capacity has been selected.
- `ApplicationTransmissionCoordinator<8,32>` composes one Application traffic reservation, aggregate/shared payload and externally supplied per-recipient OutboundDeliveryLifecycle. `ExpireDue(now)` terminalizes pending recipients at the immutable deadline and releases newly terminal aggregates' traffic reservations without silently releasing aggregate records/payload references. `BeginRecipient` immediately records `DeadlineExpired` if the immutable deadline has elapsed. `Release` now refuses non-terminal aggregates: Release is cleanup, not implicit cancellation, so a still-forwardable/retryable recipient cannot lose the aggregate's only retained payload reference or traffic ownership.
- External per-recipient OutboundDeliveryLifecycle state must be retired after its recipient becomes terminal and before aggregate Release. The coordinator deliberately does not invent cancellation semantics or own an 8×32 delivery pool.
- `IApplicationPayloadStagingBuffer` is injected composition-owned finite scratch storage with no Mesh-default byte capacity. It materializes Repeatable Serialized Source only for synchronous forwarding submission and never becomes aggregate-owned backing.
- `ApplicationForwardingCoordinator` binds one frozen recipient to the aggregate immutable payload and authenticated forwarding submission. Borrowed Stable payloads avoid application-layer/per-recipient copying; Repeatable Serialized Source uses injected staging. RadioTransport owns physical fragmentation. Terminal recipients and route/recipient mismatches are rejected before Radio submission.

## Validation status

Representative green runs: Radio deferred correlation `33956249777`; forwarding Radio-attempt `33959946510`; outbound Radio delivery `33960956661`; General CI `33961248712`; Memory Accounting `33961248739`; Clock Coordination `33961248717`; application aggregate `33963104967`; application lifecycle `33963873242`; immutable payload Topology Evolution `33964394229`; Application Forwarding `33964786992`; repeatable staging Application Forwarding `33965509782` and full CI `33965509790`; application deadline-sweep General CI `33966417039`; deadline-sweep Memory Accounting `33966417023`; Command corrective restoration `33966521660`; immediate-deadline General CI `33966745136`; immediate-deadline Memory Accounting `33966745105`.

Latest aggregate-retention production commit `602a4befcb46df997be70c32d4c9642c529079e0`; regression commit `7b9cb50ec3c9a1df2ccf8cb8bf267a7032c93c51`. Triggered runs include General CI `33966912471`, Application Forwarding `33966912396`, Memory Accounting `33966912509`, Topology Evolution `33966912423`, Clock Coordination `33966912480`, Topology Freshness `33966912376` and Control Work Lifetime `33966912400`; they were queued/in progress at this reconciliation with no failure or Actions-budget symptom observed yet.

## Unresolved public/wire decisions — do not cross silently

1. Concrete ESPressio-defined PrimitiveFamilyId allocations.
2. Canonical distributed EventTypeId derivation/allocation.
3. Event ConceptualMessageId generation scope/strategy.
4. Group identity/wire representation when Group implementation becomes unavoidable.
5. Concrete Mesh security handshake/profile/wire schema.
6. Final concrete ordinary/control/reassembly and bounded-owned payload byte capacities for whole-device DRAM budgeting.

## Immediate next work

1. Confirm the aggregate-retention runs headed by `7b9cb50ec3c9a1df2ccf8cb8bf267a7032c93c51`.
2. Continue application-delivery composition only where semantics are frozen. In particular, keep recipient terminalization, external OutboundDeliveryLifecycle retirement, aggregate cleanup and payload lifetime ordered; do not infer cancellation semantics.
3. Do not reinterpret next-hop acceptance as final destination delivery or invent completion semantics for unacknowledged multi-hop traffic.
4. Continue only Mesh work that does not require unresolved family/Event/Group/security-wire/bounded-owned-capacity decisions.
5. Keep bounded-owned payload storage capacity unresolved until explicitly approved; do not infer staging capacity as the platform-wide owned-payload capacity.
6. Do not begin Event/MeshAdapters wire integration until unresolved ID decisions are approved.

## Chronological continuation log

- 2026-09-05T08:42Z-08:50Z: Integrated explicit-capacity deferred logical-transfer correlation into RadioTransport; run `33956249777` green native + ESP32 worker.
- 2026-09-05T09:17Z-10:33Z: Added Mesh forwarding terminal correlation/attempt/outbound delivery composition; runs `33959946510` and `33960956661` green.
- 2026-09-05T11:16Z-11:39Z: Added bounded ApplicationTransmissionTable and ApplicationTransmissionCoordinator; runs `33963104967` and `33963873242` green.
- 2026-09-05T11:46Z-12:00Z: Added immutable `ApplicationPayload`, shared aggregate backing and ApplicationForwardingCoordinator; Topology Evolution `33964394229` and Application Forwarding `33964786992` green.
- 2026-09-05T12:08Z-12:10Z: Live-tip reconciliation found RoutePlanner/RouteAttemptCoordinator already present beyond an older handoff snapshot. Removed redundant tentative RoutingCoordinator in `faa75a383dc1bcdc924bcdb27a32f26caea2e63f`.
- 2026-09-05T12:11Z-12:16Z: Added composition-supplied `IApplicationPayloadStagingBuffer` and Repeatable Serialized Source forwarding through finite injected staging. Application Forwarding `33965509782` and full CI `33965509790` green.
- 2026-09-05T12:27Z: Added Command regression proving `CommandResult` is local invocation disposition only; host tests in `33966069249` passed.
- 2026-09-05T12:32Z-12:35Z: Added bounded aggregate deadline sweeping; all triggered workflows including General CI `33966417039` and Memory Accounting `33966417023` green.
- 2026-09-05T12:31Z-12:39Z: Command terminology cleanup transiently reconstructed an incomplete large header in `6c796c41f71a661ecbbb675242bc5f8f8f982df9`; exact complete core restored in `44a47e81ecdf3313e145282cedb93535cd2fa48a`; corrective run `33966521660` green. Treat transient commit as superseded, not architecture.
- 2026-09-05T12:40Z-12:42Z: Fixed immediate recipient deadline consistency in `f838eed3d3553c7fcf78b1da88c98a3bd734e97e`; regression `7c117d26630110bd275def8f25f1d55781a0cec0`; General CI `33966745136` and Memory Accounting `33966745105` green.
- 2026-09-05T12:43Z-12:45Z: Recursive application-lifecycle review identified that `ApplicationTransmissionCoordinator::Release` could discard a non-terminal aggregate and its only retained immutable payload reference while recipients remained legitimately forwardable/retryable. Corrected Release to require terminal aggregate state in `602a4befcb46df997be70c32d4c9642c529079e0`; regression `7b9cb50ec3c9a1df2ccf8cb8bf267a7032c93c51` proves premature release is rejected without releasing traffic or payload. Triggered validation still running at reconciliation.