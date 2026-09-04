# ESPressio Mesh

Bounded, hardware-agnostic multi-radio membership, topology, routing and delivery orchestration for the ESPressio Development Platform.

**Release target:** `1.0.0`

ESPressio Mesh sits above `ESPressio-Radio`. Radio moves one opaque logical transfer across one direct link; Mesh decides membership, topology, next-hop routing, retries, end-to-end delivery semantics, controlled broadcast dissemination, selective multicast resolution, and distributed control-plane behavior. Mesh does not contain hardware-specific Radio implementations and does not depend on Command, Event or State semantics.

## Architectural position

- `ESPressio-System` supplies permanent platform identity and low-level platform-neutral services.
- `ESPressio-Primitive` supplies dependency-neutral conceptual-message vocabulary.
- `ESPressio-Radio` supplies direct-link Radio abstractions.
- `ESPressio-Timing` supplies clock mathematics/discipline while Mesh coordinates distributed root/parent selection.
- `ESPressio-Security` supplies authentication/security abstractions used by admission policy.
- `ESPressio-MeshAdapters` is the optional layer that integrates Command, Event, State and future conceptual primitive families with Mesh.

Mesh itself remains unaware of Command/Event/State payload semantics. Non-Mesh primitive-family payloads are opaque to the Mesh core and are dispatched only to the receiver registered for their `PrimitiveFamilyId` at the final destination.

## Current implementation tranche

The `structural_realignment_propagation` branch is the coordinated implementation branch for the finalized 1.0.0 Mesh architecture. Its `TRANCHE_HANDOFF.MD` is the authoritative, self-contained frozen specification and chronological implementation record while the tranche remains in progress.

The current foundation now includes bounded authenticated-membership and tombstone storage, delivery deduplication and InProgress exclusion, policy-driven liveness/retention, separately bounded pre-authentication and authentication resources, authenticated admission promotion, generation-safe Radio peer bindings, incarnation-scoped `RadioIdentifier` allocation, peer-bound neighbour discovery, bounded primitive-family receiver registration, and protected traffic-governor capacities. All of these components remain narrow services intended to compose inside the serialized Mesh execution domain rather than becoming independent scheduling layers.

Control work is also required to have a finite lifetime. `IControlWorkLifetimePolicy` supplies those local operational lifetimes, while `FixedControlWorkLifetimePolicy` provides an explicit composition-root implementation without inventing universal timeout values. Application deliveries are deliberately excluded because they retain their own immutable delivery deadline.

## Core identity rules

`DeviceIdentifier` is owned by `ESPressio-System` and permanently identifies the device. Mesh adds separate identities for Mesh domain and participation lifecycle:

- `MeshIdentifier`: exact 16-byte application-supplied Mesh identity; all-zero is invalid/unspecified.
- `MembershipIncarnation`: exact 16-byte participation-instance identity; a partition does not change it.
- `MeshNodeAlias`: 16-bit Mesh-local routing handle only; it is never authority or permanent identity.
- `RadioIdentifier`: 8-bit node-local Radio handle; 1–254 are usable and are never recycled within one `MembershipIncarnation`.
- `MeshMessageId`: 64-bit identity of one independently routable delivery or one Broadcast.
- `ProfileGeneration` and `TopologyGeneration`: independent 64-bit generation domains.

`CanonicalName` is a mandatory bounded human-readable profile property, not identity. Its semantic representation is one length byte plus 32 bytes of backing storage. Valid names contain 1–32 printable ASCII bytes, compare exactly/case-sensitively, and cannot begin/end with a space.

Radio-owned `RadioPeerHandle` values are deliberately separate from every Mesh identity. They are process-local, generation-safe direct-link handles supplied by `ESPressio-Radio`; Mesh may retain them beside a `RadioIdentifier` as link evidence, but they are never distributed or treated as authenticated node identity.

## Frozen default bounds

The baseline 1.0.0 configuration is intentionally bounded. Among the locked defaults are 32 Mesh members, four Radios per member, eight Groups per member, eight primitive receivers, eight active application transmission aggregates, 96 topology links, 16 route hops, a 16-hop initial forwarding limit, 32 cached routes, 32 maximum recipients in one selective-multicast aggregate and 64 membership tombstones.

Traffic governance protects four independent local capacities: eight Infrastructure Responses, four Clock Control items, eight General Control items and eight Application transmission aggregates. Application saturation cannot borrow from the control reserves. Control items additionally receive finite lifetimes through `IControlWorkLifetimePolicy`; no control queue is permitted to retain work indefinitely.

These bounds are semantic defaults rather than permission to allocate unbounded dynamic storage elsewhere. Every retained queue, retry set, reassembly set and control-work pool must remain finite and expose deterministic backpressure/exhaustion behavior.

## Memory accounting

`MeshFixedMemoryAccounting<TTopologyCharacteristics>` exposes target-native `sizeof` accounting for the principal stores whose cardinalities are already frozen: authenticated membership/liveness/tombstones, inbound delivery reservations, pending neighbour candidates, inbound authentications, liveness probes, authenticated direct-peer bindings, the global topology graph, route cache, primitive receiver registry and default traffic governor.

The values are deliberately evaluated by the target compiler. A host x86-64 result is useful for regression but is **not** an ESP32 memory budget because pointer width/alignment and the application's topology-characteristics type can differ. Delivery-acknowledgement storage is reported only after the composition root supplies an explicit finite acknowledgement capacity; no universal capacity is invented by the library.

Whole-device planning must add task stacks, RadioTransport/provider storage, payload/reassembly/control buffers, security-authority private state and application objects. This keeps the memory model measurable without disguising unresolved application capacity choices as architectural defaults.

## Selective multicast and Broadcast

Group and CapabilitySelector destinations are resolved once at the sender into a frozen bounded recipient set. Each resolved recipient receives an independent Node delivery with its own `MeshMessageId` and outcome state, while all recipient deliveries may share one immutable payload backing.

Broadcast is different: it is one bounded best-effort controlled flood with one `MeshMessageId`, no frozen recipient set and no promise of delivery to every member. Applications requiring per-member delivery knowledge use selective multicast rather than reliable Broadcast.

## Platform independence

Mesh contains no Arduino, ESP-IDF, FreeRTOS or Radio-hardware API calls. ESP32 Raw80211, NRF24 and future Radio concretes belong in their platform/technology repositories. ESP32-specific shared-Wi-Fi-PHY arbitration belongs in `ESPressio-ESP32`; channel changes appear to Mesh only as link/topology availability changes.

## Development dependencies

During this coordinated implementation tranche, Mesh consumes the matching propagation branches of repositories whose contracts are changing:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-System.git#structural_realignment_propagation_ESPressio-Mesh
    https://github.com/ESPressio-Development-Platform/ESPressio-Primitive.git#structural_realignment_propagation_ESPressio-Mesh
    https://github.com/ESPressio-Development-Platform/ESPressio-Radio.git#structural_realignment_propagation_ESPressio-Mesh
```

Other dependencies remain on their current `structural_realignment` Working Branch until this tranche actually requires a reciprocal propagation change.