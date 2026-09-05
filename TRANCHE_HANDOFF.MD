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

Radio: complete immutable bounded logical transfer to directly reachable Radio peer; Radio owns fragmentation/reassembly/physical retries and peer identity. RadioPeerHandle generation-safe/process-local. Send acceptance is admission only; terminal evidence distinguishes transmission completion and peer acknowledgement where supported. Precision timing path remains separate.

Traffic/execution: injected IMeshTrafficGovernor with protected capacities Infrastructure8, Clock4, General8, Application8. Control work finite lifetime. Serialized Mesh mutable-state domain acceptable; no direct FreeRTOS in Mesh.

Payload models: Borrowed Stable Payload, Bounded Owned Payload, Repeatable Serialized Source. Finite length known before acceptance. One MessageId maps to immutable logical bytes through retries/routes/fragments. Selective multicast recipients share one payload backing. Bounded-owned byte capacity remains deliberately unresolved and must not be invented. Repeatable Serialized Source forwarding may use a composition-supplied finite `IApplicationPayloadStagingBuffer`; Mesh defines no default staging capacity.

Locked capacities: MaxMeshNodes32, MaxRadiosPerNode4, MaxGroupsPerNode8, MaxPrimitiveReceivers8, MaxActiveApplicationTransmissions8, MaxTopologyLinks96, MaxRouteHops16, DefaultHopLimit16, route cache32, MaxActiveInboundDeliveries8, MaxPendingNeighbourCandidates8, MaxActiveLivenessProbes4, MaxActiveInboundAuthentications4, MaxRecipientsPerTransmission32, MaxMembershipTombstones64, dedup128, same-route attempts3, routes4.

## Implemented tranche state

System: canonical exact-16-byte DeviceIdentifier implemented; platform derivation outside System.
Primitive: family/version/message/correlation/fingerprint/range contracts implemented; no concrete family ID allocated.
Radio/ESP32/NRF24: direct-peer complete logical transfer, bounded peer registry, fragmentation/reassembly, peer lifecycle, deferred terminal correlation, explicit admission-vs-terminal evidence, ESP32 shared-PHY coordination implemented.
State: canonical StateAddress, local authority binding, remote replicas, availability/reachability, epoch/revision, serialized observers and v2 transport primitives implemented.
Command: intrinsic RPC machinery removed; CommandMessage is conceptual/correlation/version + bounded payload. README/wiki and the core `CommandResult` doc comments define `CommandResult` as local invocation disposition only. A dedicated regression proves `CommandResult::Ok()` may be returned while the initiated application operation remains incomplete, and `Error()` likewise describes only local handler rejection/failure. No operation-completion contract remains in the result documentation.
Event: transport provenance removed from Event object state; Local/Remote dispatch context and EVTT v2 implemented. Distributed EventTypeId and conceptual MessageId strategy remain unresolved.

Mesh:
- Membership tombstones/dedup/message IDs/inbound coordination/authenticated membership/retention/liveness/lifecycle/pre-auth admission/security boundaries implemented.
- Primitive receiver/support snapshot, neighbour discovery, Mesh Radio registry, peer lifecycle and authenticated direct-peer bindings implemented.
- Directed topology/freshness/publication/graph, route/cache, injected routing/planner, bounded attempt/retry and forwarding submission/transition implemented.
- Radio terminal correlation, delivery ACK tracking/coordinator and outbound delivery lifecycle implemented without conflating admission, link evidence, destination acceptance or application completion.
- Clock coordination/membership/reachability/parent-usability foundations implemented.
- Target-native fixed/cardinality memory accounting uses sizeof; variable payload/reassembly/security-provider storage remains separate.
- `ApplicationTransmissionTable<8,32>` owns bounded selective aggregates with generation-safe handle, immutable deadline, frozen unique recipients, independent MessageIds/outcomes and exactly one immutable `ApplicationPayload` reference shared by all recipients. It owns no variable-capacity payload bytes, routes, Radio state, ACK tracker, selector identity or Broadcast semantics. A bounded `ExpireDue(now)` sweep marks every still-pending recipient of elapsed aggregates `DeadlineExpired` while retaining terminal records for caller inspection.
- `ApplicationPayload` represents Borrowed Stable bytes or a non-owning `IRepeatableSerializedPayloadSource`. Both expose finite size before admission and immutable repeatable bytes for the complete aggregate lifetime. No payload byte capacity was selected. A future bounded-owned backing can satisfy the same aggregate contract once its capacity is explicitly approved.
- `ApplicationTransmissionCoordinator<8,32>` composes one Application traffic reservation, one aggregate/shared payload and externally supplied per-recipient OutboundDeliveryLifecycle. It exposes the same aggregate payload to every recipient, introduces no hidden 8*32 active-delivery pool, and now performs a bounded deadline sweep which releases each newly terminal aggregate's Application traffic reservation without silently releasing its aggregate record/payload reference.
- `IApplicationPayloadStagingBuffer` is an injected composition-owned finite scratch buffer with no Mesh-default byte capacity. It exists only to materialize a Repeatable Serialized Source for synchronous forwarding submission and never becomes aggregate-owned payload backing.
- `ApplicationForwardingCoordinator` composes a frozen recipient, aggregate immutable payload and authenticated forwarding submission. Borrowed Stable payloads are passed directly into RadioTransport without an application-layer/per-recipient copy or re-serialization. Repeatable Serialized Source may be materialized into injected staging; missing, unavailable, undersized or failed serialization paths return explicit dispositions. RadioTransport remains responsible for physical fragmentation. Terminal recipients and route/recipient mismatches are rejected before Radio submission.

## Validation status

Representative green runs: Radio deferred correlation `33956249777`; forwarding Radio-attempt `33959946510`; outbound Radio delivery `33960956661`; General CI `33961248712`; Memory Accounting `33961248739`; Clock Coordination `33961248717`; application aggregate `33963104967`; application lifecycle `33963873242`.

Immutable-payload continuation:
- First Topology Evolution `33964359407` compiled and ran every regression through the application aggregate successfully, then failed only `-Werror=misleading-indentation` in the newly compacted ApplicationTransmissionCoordinator. No semantic/test failure occurred before that point.
- Warning formatting corrected in commit `5ec5fea8c8a6483e9b39b68e83d4946109e9b5`.
- Follow-up Topology Evolution `33964394229` completed successfully across the full topology/routing/forwarding/ACK/application/security/umbrella suite.
- Initial Application Forwarding run `33964689414` compiled successfully but exposed an incorrect test assumption: it compared the application backing pointer with the concrete provider's physical-packet pointer even though RadioTransport correctly owns fragmentation and constructs its own physical packet. Production code was not changed for this finding.
- Regression corrected in commit `d3a541e9b9345dcf2b13dc9d2eaee40c19ac927e` to verify that the aggregate continues to alias the original immutable logical backing while the provider receives a Radio-owned physical fragment packet.
- Follow-up Application Forwarding run `33964786992` passed build, corrected application-forwarding regression and current Mesh umbrella.
- Live-tip reconciliation on 2026-09-05 detected that `RoutePlanner` and `RouteAttemptCoordinator` were already present and validated beyond the older handoff snapshot. A redundant tentative `RoutingCoordinator` created while resuming was immediately removed in commit `faa75a383dc1bcdc924bcdb27a32f26caea2e63f`; no production routing semantics changed.
- Repeatable staging head `e969ada33c4c799169f2a03a7d79c2b6cc9c1c92`: dedicated Application Forwarding run `33965509782` passed; full CI `33965509790` passed; Topology Freshness `33965509791`, Control Work Lifetime `33965509787`, Clock Coordination `33965509804`, Topology Evolution `33965509786` and Memory Accounting `33965509784` also passed. No Actions-budget symptom occurred.
- Command local-disposition regression commit `b098e92aae1a5bf7c35f84ba082835a27501d5ee`: host-tests in run `33966069249` passed.
- Application deadline sweep head `1a593a095cdb88b938d1e716e5d37fc2b54426ea`: General CI `33966417039`, Topology Evolution `33966416998`, Application Forwarding `33966416983`, Topology Freshness `33966417004`, Control Work Lifetime `33966417009` and Clock Coordination `33966416993` passed. Memory Accounting `33966417023` was still running at reconciliation time; no failure/budget symptom was observed.
- During the terminology-only Command edit, an incomplete reconstruction of the large header was accidentally committed as `6c796c41f71a661ecbbb675242bc5f8f8f982df9`; Actions immediately exposed missing existing APIs. The complete pre-edit core was restored from its exact prior blob and only the intended local-disposition comments retained in corrective commit `44a47e81ecdf3313e145282cedb93535cd2fa48a`. Corrective run `33966521660` was queued at reconciliation time. Treat the transient failed commit as superseded, not as intended architecture.

## Unresolved public/wire decisions — do not cross silently

1. Concrete ESPressio-defined PrimitiveFamilyId allocations.
2. Canonical distributed EventTypeId derivation/allocation.
3. Event ConceptualMessageId generation scope/strategy.
4. Group identity/wire representation when Group implementation becomes unavoidable.
5. Concrete Mesh security handshake/profile/wire schema.
6. Final concrete ordinary/control/reassembly and bounded-owned payload byte capacities for whole-device DRAM budgeting.

## Immediate next work

1. Confirm corrective Command run `33966521660` is fully green; if not, fix only issues attributable to the terminology edit/restoration before proceeding.
2. Confirm Mesh Memory Accounting run `33966417023` after the application deadline sweep. The sweep adds no new retained storage and must not change fixed-cardinality accounting.
3. Continue only Mesh work that does not require unresolved family/Event/Group/security-wire/bounded-owned-capacity decisions. Repeatable Serialized Source forwarding is no longer blocked when composition supplies finite staging.
4. Keep bounded-owned payload storage capacity unresolved until explicitly approved; do not infer staging capacity as the platform-wide owned-payload capacity.
5. Do not begin Event/MeshAdapters wire integration until unresolved ID decisions are approved.

## Chronological continuation log

- 2026-09-05T08:42Z-08:50Z: Integrated explicit-capacity deferred logical-transfer correlation into RadioTransport; run `33956249777` green native + ESP32 worker.
- 2026-09-05T09:17Z-10:33Z: Added Mesh forwarding terminal correlation/attempt/outbound delivery composition; runs `33959946510` and `33960956661` green.
- 2026-09-05T11:16Z-11:39Z: Added bounded ApplicationTransmissionTable and ApplicationTransmissionCoordinator; corrected one test namespace typo; runs `33963104967` and `33963873242` green.
- 2026-09-05T11:46Z-11:51Z: Added `ApplicationPayload`, Borrowed Stable and Repeatable Serialized Source contracts; bound exactly one immutable payload reference to each aggregate; updated table/coordinator regressions and umbrella export. No byte-capacity constant introduced. Initial run `33964359407` exposed only warning-clean formatting in the coordinator after all preceding tests passed; corrected in `5ec5fea8c8a6483e9b39b68e83d4946109e9b5`. Follow-up Topology Evolution `33964394229` completed successfully.
- 2026-09-05T11:55Z-12:00Z: Added `ApplicationForwardingCoordinator` for Borrowed Stable recipient submission through authenticated forwarding; route/destination and terminal-recipient guards prevent accidental submission; Repeatable Serialized Source initially returned explicit `StagingRequired`. Initial dedicated run `33964689414` found only a regression-fixture boundary mistake (application logical pointer was incorrectly expected to equal Radio provider physical-fragment pointer). Corrected the test without changing production semantics in `d3a541e9b9345dcf2b13dc9d2eaee40c19ac927e`; follow-up run `33964786992` passed build, regression and umbrella.
- 2026-09-05T12:08Z-12:10Z: Reconciled against the live branch tip and discovered routing planner/attempt orchestration had already advanced beyond the stale handoff snapshot. Removed the just-created redundant `RoutingCoordinator` in `faa75a383dc1bcdc924bcdb27a32f26caea2e63f` rather than duplicating validated responsibility.
- 2026-09-05T12:11Z-12:16Z: Added composition-supplied `IApplicationPayloadStagingBuffer` and Repeatable Serialized Source forwarding through finite injected staging. No default staging/owned-payload capacity was introduced. Explicit outcomes cover staging required, insufficient capacity, unavailable storage and serialization failure. Dedicated Application Forwarding `33965509782`, full CI `33965509790` and all other triggered Mesh workflows passed on head `e969ada33c4c799169f2a03a7d79c2b6cc9c1c92`.
- 2026-09-05T12:27Z: Added a Command regression proving local `CommandResult::Ok()` is compatible with an application operation that is still incomplete, while `Error()` remains local rejection/failure only. Commit `b098e92aae1a5bf7c35f84ba082835a27501d5ee`; host portion of run `33966069249` passed.
- 2026-09-05T12:32Z-12:35Z: Added bounded aggregate deadline sweeping to ApplicationTransmissionTable/Coordinator. `ExpireDue(now)` marks only pending recipients DeadlineExpired, releases the aggregate's Application traffic reservation once terminal, preserves the aggregate/payload for explicit inspection/release, and is bounded by the locked eight active aggregates. Head `1a593a095cdb88b938d1e716e5d37fc2b54426ea`; General CI `33966417039`, Topology Evolution `33966416998`, Application Forwarding `33966416983`, Topology Freshness `33966417004`, Control Work Lifetime `33966417009` and Clock Coordination `33966416993` green.
- 2026-09-05T12:31Z-12:37Z: Performed the remaining CommandResult terminology cleanup. An incomplete large-header reconstruction in transient commit `6c796c41f71a661ecbbb675242bc5f8f8f982df9` was caught immediately by Actions; restored the exact complete pre-edit core and retained only the intended documentation changes in `44a47e81ecdf3313e145282cedb93535cd2fa48a`. Corrective run `33966521660` queued.