# ESPressio Mesh

Bounded, hardware-agnostic multi-radio membership, topology, routing and delivery orchestration for the ESPressio Development Platform.

## Architectural position

`ESPressio-Mesh` sits above `ESPressio-Radio` and below optional Primitive-family integration in `ESPressio-MeshAdapters`.

- `ESPressio-System` owns permanent device identity and platform-neutral system services.
- `ESPressio-Primitive` supplies family-neutral conceptual-message vocabulary and delivery-policy contracts.
- `ESPressio-Radio` owns direct-link logical-transfer transport, R3 physical fragmentation/reassembly/arbitration and precision direct-neighbour timestamp exchange.
- `ESPressio-Timing` owns clock estimation, filtering, discipline, uncertainty, reliability and adaptive synchronization scheduling.
- `ESPressio-Security` supplies cryptographic/security abstractions.
- `ESPressio-Mesh` owns mesh membership, authenticated topology, next-hop routing, bounded forwarding, admission, broadcast dissemination and clock-reference topology selection.
- `ESPressio-MeshAdapters` optionally integrates Event, Command, State and future conceptual Primitive families. Mesh core does not depend on those family runtimes and does not inspect their payload semantics.

Non-Mesh Primitive-family payloads remain opaque to Mesh. At a destination, Mesh dispatches bytes only through the receiver registered for the corresponding `PrimitiveFamilyId`; the family runtime alone decides the exact M1 admission disposition.

## Current redesign branch

The active implementation branch for the Primitive Platform Redesign is `primitives_redesign`.

The authoritative cross-repository architecture/governance document is the Primitive Platform Redesign handoff. The current live continuation state is maintained in `ESPressio-Primitive/primitives_redesign/TRANCHE_HANDOFF_CURRENT.md`. This repository's historical `TRANCHE_HANDOFF.MD` remains useful implementation history, but it does not override the locked cross-repository architecture or current branch tips.

The library version remains `1.0.0`; the redesign does not authorize a version change.

## Identity and addressing

`DeviceIdentifier` is owned by `ESPressio-System` and permanently identifies a device. Mesh adds separate domain and participation identities:

- `MeshIdentifier` — exact 16-byte application-supplied Mesh identity.
- `MembershipIncarnation` — exact participation-instance identity; it is never a System runtime incarnation.
- `GroupIdentifier` — non-zero opaque group identity scoped by the containing Mesh.
- `MeshNodeAlias` — Mesh-local routing handle only, never authority or permanent identity.
- `RadioIdentifier` — node-local Radio handle, separate from every Mesh identity.
- `MeshMessageId` — identity of one independently routable Mesh delivery or Broadcast.

Radio-owned `RadioPeerHandle` values are generation-safe direct-link facts. They are not distributed semantic identity and must never be substituted for `DeviceIdentifier` or `MembershipIncarnation`.

## Primitive admission and evidence

Mesh uses the exact family-neutral M1 result set:

- `Accepted`
- `AlreadyAccepted`
- `TemporarilyUnavailable`
- `ResourceUnavailable`
- `Unsupported`
- `Rejected`
- `Malformed`

Only `Accepted` and `AlreadyAccepted` establish `DestinationPrimitiveAdmission`. Queue ownership, Radio acceptance, forwarding, physical transmission and local scheduling are never promoted into Primitive admission evidence.

The destination family receives authenticated semantic provenance separately from immediate transport/peer facts. Mesh membership incarnation is not a substitute for `System::RuntimeIncarnationId`.

## Bounded forwarding and relay ownership

Mesh forwarding is bounded and ownership-based.

A relay accepts responsibility only after it has atomically acquired the complete local resources required for retained work: relay record, byte storage and workspace. It never accepts metadata while deferring byte ownership to an unbounded or hidden allocation path.

Relay capacity is described by explicit `MeshRelayCapacityProfile` values. Membership admission must reject incompatible peers when their advertised minimum relay-capacity requirements cannot be met.

Remaining residence is finite and non-increasing. A forwarded/retried item cannot gain lifetime during duplication, relay or retry processing.

Radio remains the sole owner of physical fragmentation, reassembly and arbitration. Mesh hands Radio one logical transfer and does not implement a competing fragment scheduler.

## Broadcast semantics

Broadcast is one bounded controlled flood with one source-scoped `MeshMessageId`.

The network `Seen`/`Forwarded` lifecycle is independent of local Primitive-family admission. A verified unseen Broadcast is committed to the bounded deduplication lifecycle before fan-out. If local family admission is temporarily unavailable, Mesh may retain only bounded `DeferredLocal` state; servicing that deferred local delivery must not re-fan the Broadcast.

A source-originated outbound Primitive is never fed back into its own family runtime by Mesh. Local dispatch belongs to the originating family/composition and is explicitly excluded when already performed.

Generic Broadcast is permitted only where the family policy makes it legal. In particular:

- response-bearing Command requests/responses are not generic Broadcast traffic;
- canonical State V1 traffic is not generic Broadcast traffic;
- a family occurrence requiring destination Primitive admission evidence cannot obtain that evidence from generic Broadcast.

## Mesh v1 security

Mesh v1 authenticates provisioned device identities and derives pairwise sessions without exposing private/session keys to Mesh core.

The security boundary is `IMeshV1CryptographicProvider`. Mesh v1 protects direct-hop and End-to-End purposes separately, uses replay windows committed only after successful authentication, and binds security state to the exact Mesh/device/membership context.

For multi-hop Broadcast, each hop authenticates the direct sender while the signed immutable origin frame authenticates the original source. A relay/destination must know the original source as an active authenticated Mesh member; this does not imply that the original source is a direct neighbour.

## Clock ownership

Clock responsibilities are deliberately split:

- Mesh owns authenticated root/parent/reference topology selection.
- Radio owns precision direct-neighbour timestamp exchange.
- Timing owns estimator state, offset/delay validation, filtering, drift learning, uncertainty, reliability and clock discipline.

`MeshSystemClockSynchronizationCoordinator` accepts a selected root/parent relationship and configures a fixed `IMeshClockReferenceTransportControl`. Radio-backed composition implements that control surface and feeds direct-link observations into Timing. Mesh never runs the timestamp exchange, chooses estimator cadence, computes clock offset, or invents `TimeReliability`.

`MeshClockReferenceLineage` carries the authenticated upstream reference identity, reliability and uncertainty required to prevent a node from presenting itself as a better reference than its upstream evidence permits. Changing synchronization source selects a new Timing reference; root/source changes therefore discard source-specific estimator history rather than blending independent clock sources.

Deadline-bearing Mesh work may rely on the system clock only while the composed Timing state says it is usable.

## Runtime worker

The canonical Mesh runtime worker is `MeshRuntimeThread`, built on the generic ESPressio Thread/Task substrate. It uses wake/deadline driven service and bounded work callbacks.

The redesign does not retain the predecessor `PrecisionThread`, `std::function` callback storage, maintenance-period polling loop, or catch-all exception wrapper. CI strips comments before checking that those predecessor mechanisms are absent from executable code.

## Resource and memory accounting

Mesh accounting reports resources that Mesh itself owns. Radio-owned R3 fragmentation/reassembly/scheduler/provider resources are deliberately not repeated as Mesh memory.

`MeshFixedMemoryAccounting<TTopologyCharacteristics>` exposes target-native `sizeof` accounting for the principal fixed-cardinality Mesh stores, including authenticated membership/liveness/tombstones, admission reservations, direct-peer bindings, security sessions, topology, route cache, Primitive receiver registry and traffic governance.

`MeshRuntimeMemoryAccounting` composes those principal stores with the selected clock/acknowledgement state, Mesh-owned byte pools, relay planes, frame/workspace ownership, security-composition storage, the generic runtime-worker object and explicit Task stack reserve. `TotalMeshReservedBytes` is therefore a deterministic Mesh-owned reserve for the selected composition profile; it is not a whole-device RAM figure and does not double-count Radio-owned resources.

`MeshPlatformCapacityProfile` makes Mesh-owned byte capacities and composition reserves explicit. Any Radio capability values recorded by a platform profile are compatibility expectations for composition, not permission for Mesh to account or manage Radio's physical resources.

Host accounting remains ABI-specific regression evidence. Target builds must evaluate the same templates with the target compiler when a target-native RAM budget is required.

## Controlled runtime reset

`MeshRuntimeResetCoordinator` provides deterministic local teardown for Mesh-owned membership, reservations, topology, route, acknowledgement/correlation, security-session, clock-observation and traffic-governor state.

Shutdown ordering remains explicit: lower/direct-link transports stop first so callbacks cannot repopulate Mesh state; application composition retires exact per-recipient work; Mesh then clears its remaining bounded state. Reset does not fabricate delivery, admission, Radio-terminal, membership or clock evidence.

## Platform independence

Mesh contains no Arduino, ESP-IDF, FreeRTOS or Radio-hardware API calls. ESP32/NRF24/other Radio concretes remain in their platform/technology repositories. Platform-specific coexistence/arbitration remains below Mesh and appears to Mesh only through authenticated link/topology and Radio service availability.

## Dependencies

The `primitives_redesign` manifest intentionally depends only on the family-neutral/core contracts required by Mesh:

```text
ESPressio-System
ESPressio-Threads
ESPressio-Primitive
ESPressio-Radio
ESPressio-Timing
ESPressio-Security
```

Mesh core must not acquire dependencies on `ESPressio-Event`, `ESPressio-Command`, `ESPressio-State`, `ESPressio-Observable` or `ESPressio-Adapters`. Optional Primitive-family composition belongs in `ESPressio-MeshAdapters`.

## Tranche-8 validation

The Tranche-8 closure workflow validates the locked replacement architecture as contracts rather than as predecessor-API compatibility tests. It covers:

- exact M1 admission/evidence behavior;
- complete relay record+byte+workspace ownership;
- relay-capacity compatibility;
- finite remaining residence;
- forward-once and bounded `DeferredLocal` behavior;
- family Broadcast legality and source no-feedback;
- logical Mesh-to-Radio handoff without physical-fragment ownership leakage;
- security/replay/authentication behavior;
- deterministic codec fuzzing;
- a three-node forward-once fixture;
- clock ownership/failover;
- the generic Thread runtime worker;
- deterministic Mesh resource accounting;
- dependency and predecessor-runtime guards;
- unchanged `1.0.0` manifest version.

See `TRANCHE_8_CLOSURE.md` for the exact closure evidence and workflow identifiers once the tranche is formally closed.
