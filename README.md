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

Every accepted application transmission retains a wire-neutral `ApplicationPrimitiveDescriptor` alongside its immutable payload reference. The descriptor carries the non-Mesh `PrimitiveFamilyId` and family protocol version needed by later framing and destination dispatch; it deliberately defines no packet layout. Mesh Control cannot enter through this application path.

## Current implementation tranche

The `structural_realignment_propagation` branch is the coordinated implementation branch for the finalized 1.0.0 Mesh architecture. Its `TRANCHE_HANDOFF.MD` is the authoritative, self-contained frozen specification and chronological implementation record while the tranche remains in progress.

The current foundation now includes bounded authenticated-membership and tombstone storage, delivery deduplication and InProgress exclusion, policy-driven liveness/retention, separately bounded pre-authentication and authentication resources, authenticated admission promotion, generation-safe Radio peer bindings, incarnation-scoped `RadioIdentifier` allocation, peer-bound neighbour discovery, bounded primitive-family receiver registration, protected traffic-governor capacities, directed topology/routing foundations, authenticated forwarding/delivery lifecycle, aggregate-aware selective application delivery, and bounded clock root/parent coordination. All of these components remain narrow services intended to compose inside the serialized Mesh execution domain rather than becoming independent scheduling layers.

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

## Mesh v1 security profile

Mesh v1 authenticates provisioned long-term device identities while deriving fresh ephemeral pairwise sessions. The frozen suite is ECDSA P-256/SHA-256 for identity signatures, ephemeral ECDH P-256, HKDF-SHA-256 for session derivation, and AES-256-GCM with a 96-bit nonce and 128-bit tag for protected traffic. This follows the key-establishment, signature, derivation and authenticated-encryption constructions specified by NIST SP 800-56A Rev. 3, FIPS 186-5, RFC 5869 and NIST SP 800-38D respectively.

`MeshV1SecurityHandshakeCodec` defines canonical network-byte-order frames rather than serializing native C++ layout. Its exact v1 packets are a 219-byte signed InitiatorHello, 267-byte signed and key-confirmed ResponderHello, and 58-byte InitiatorFinish key confirmation. Both signed hellos bind `MeshIdentifier`, device identity, membership incarnation, an uncompressed ephemeral P-256 public key and a 32-byte nonce; the responder also binds the complete initiator-hello digest. Both sides explicitly confirm the digest of the signed hellos under distinct directional confirmation material. Unknown suites, wrong lengths, trailing bytes, zero identities and malformed public-key encodings are rejected.

`IMeshV1CryptographicProvider` is the only private-key/algorithm boundary. It resolves registered device keys, owns ephemeral secrets and derived traffic keys behind generation-safe handles, and supplies entropy, hashing, signing, verification, derivation and AEAD operations. Mesh never treats `DeviceIdentifier` as a credential and never receives raw private/session keys. HKDF context binds the Mesh, an application-configured 32-byte `MeshSecurityChannelBinding`, ordered authenticated identities/incarnations, both nonces, roles and full signed transcript; distinct directional Hop, EndToEnd and KeyConfirmation keys and base IVs prevent cross-purpose key/nonce reuse. The channel binding is not transmitted: changing the mesh-wide Channel changes key derivation and causes confirmation to fail between differently configured nearby meshes, while physical channel mapping remains platform/Radio policy.

`MeshSecuritySessionTable` retains at most one current pairwise session per Mesh member by default. Hop and end-to-end traffic have independent non-wrapping outbound sequences and 64-position inbound replay windows. Replay state is preflighted before decryption but committed only after successful AEAD authentication. Replacing an incarnation/session and controlled shutdown synchronously release provider-owned secret state; stale handles cannot address the replacement.

`MeshV1ResponderAdmissionCoordinator` and `MeshV1InitiatorAdmissionCoordinator` are the two bounded handshake directions. Their record counts each equal the active inbound-authentication bound because a remote identity claim remains untrusted regardless of which node initiated the exchange. The responder verifies the signed InitiatorHello before deriving a session and admits only after the InitiatorFinish confirms the complete transcript. The initiator emits its canonical signed hello, verifies the signed and key-confirmed ResponderHello, and exposes an immutable InitiatorFinish. Composition must call `MarkInitiatorFinishSubmitted` only after the exact finish has been accepted for outbound submission; this permits local admission but does not claim peer receipt or remote promotion.

Both directions use `CompleteMeshV1AdmissionTransaction` for the same serialized admission-policy, membership/session-capacity and ownership-transfer commit. A derived provider session remains staged until that transaction succeeds. Deferral, rejection, timeout and terminal failure release it before changing pre-authentication state; session saturation retains exact authenticated work for retry; successful promotion alone transfers it into `MeshSecuritySessionTable` and may return the exact authenticated direct-peer binding. Failed provider cleanup retains the owning state and reservation so expiry or controlled reset can retry without losing a secret handle. `MeshPendingAuthenticationResetGroup` composes both directional owners behind the runtime reset's single pending-authentication boundary; staged secrets are released before the provider-wide reset, after which stale records are unconditionally forgotten because the provider has erased all secret slots.

`MeshV1ProtectedApplicationSubmissionCoordinator` is the only application-payload forwarding surface. It resolves the frozen recipient and route, requires established destination and next-hop sessions, authenticates the immutable absolute deadline inside the EndToEnd header, wraps that frame in the current Hop session, and submits only the complete opaque Hop packet to Radio. `MeshV1ProtectedDestinationCoordinator` authenticates Hop before EndToEnd, commits each purpose-specific replay window only after successful opening, then applies authenticated membership/deduplication reservation before synchronous primitive dispatch. Receiver backpressure releases only the semantic reservation; definitive dispatch, unsupported family/version and authenticated deadline expiry commit deduplication. The earlier plaintext forwarding/staging surface has been removed.

## Frozen default bounds

The baseline 1.0.0 configuration is intentionally bounded. Among the locked defaults are 32 Mesh members, four Radios per member, eight Groups per member, eight primitive receivers, eight active application transmission aggregates, 96 topology links, 16 route hops, a 16-hop initial forwarding limit, 32 cached routes, 32 maximum recipients in one selective-multicast aggregate and 64 membership tombstones.

Traffic governance protects four independent local capacities: eight Infrastructure Responses, four Clock Control items, eight General Control items and eight Application transmission aggregates. Application saturation cannot borrow from the control reserves. Control items additionally receive finite lifetimes through `IControlWorkLifetimePolicy`; no control queue is permitted to retain work indefinitely.

These bounds are semantic defaults rather than permission to allocate unbounded dynamic storage elsewhere. Every retained queue, retry set, reassembly set and control-work pool must remain finite and expose deterministic backpressure/exhaustion behavior.

## Clock coordination

Mesh coordinates distributed clock root and parent choice while leaving synchronization mathematics and discipline to `ESPressio-Timing` and precision direct-link timestamp mechanics to `ESPressio-Radio`.

`ClockCoordinationTable<TQuality>` is a fixed-capacity informational store, bounded by `MaxMeshNodes` by default. The quality representation is deliberately application/composition-defined rather than imposed by Mesh. Root eligibility, quality comparison, root election and parent selection are injected independently through `IClockEligibilityPolicy`, `IClockQualityPolicy`, `IClockRootElectionPolicy` and `IClockParentSelectionPolicy`.

The supplied default election behavior is quality-first with deterministic `DeviceIdentifier` tie-breaking. Parent choice prefers a lower stratum, then better root quality, then `DeviceIdentifier`. A node which elects itself as root has no parent; a non-root parent candidate must advertise the same elected root and becomes the upstream node from which the local stratum is derived. New authenticated membership incarnations replace old informational observations; monotonic observation time cannot regress within one incarnation.

The clock-coordination types define no Mesh control-family number and no wire encoding. They own no Radio exchange, task, timer or Timing discipline. Precision T1/T2/T3/T4 exchange remains a separate protected Radio/Timing path and does not become ordinary Mesh forwarding traffic.

## Memory accounting

`MeshFixedMemoryAccounting<TTopologyCharacteristics>` exposes target-native `sizeof` accounting for the principal stores whose cardinalities are already frozen: authenticated membership/liveness/tombstones, inbound delivery reservations, pending neighbour candidates, inbound authentications, liveness probes, authenticated direct-peer bindings, pairwise security sessions, the global topology graph, route cache, primitive receiver registry and default traffic governor.

The values are deliberately evaluated by the target compiler. A host x86-64 result is useful for regression but is **not** an ESP32 memory budget because pointer width/alignment and application-selected representations can differ. Delivery-acknowledgement storage is reported only after the composition root supplies an explicit finite acknowledgement capacity, and clock-coordination storage is reported only after the composition supplies its `TClockQuality` representation. No universal capacity or quality structure is invented by the library.

The dedicated ESP32 accounting probe uses PlatformIO `espressif32` 7.1.0, Arduino-ESP32 `3.20017.241212+sha.dcc1105b` and the Xtensa ESP32 GCC 8.4.0 toolchain. It emits a distinct retained symbol for every principal store—including application aggregate metadata and pairwise Mesh security sessions—plus the representative clock-quality table and eight-entry delivery-acknowledgement tracker. The workflow derives the target-native totals from the resulting ELF so newly added retained state cannot remain absent from a hard-coded subtotal.

The corresponding x86-64 values remain ABI-specific regression data rather than ESP32 estimates. These measurements describe retained structure/cardinality storage only, not complete runtime or whole-device RAM usage.

Whole-device planning must add task stacks, RadioTransport/provider storage, payload/reassembly/control buffers, protected-frame workspace, security-authority private state and application objects. The ESP32 probe firmware's aggregate framework/build RAM figure is intentionally not used as a Mesh budget, because it also includes Arduino/framework runtime and the deliberately materialized probe arrays. This keeps the memory model measurable without disguising unresolved application capacity choices as architectural defaults.

`MeshPlatformCapacityProfile` now makes those retained-byte choices build-visible. A platform composition supplies a stable non-zero profile identifier, maximum bytes for each of the eight inbound delivery slots, each protected control slot and each of the eight bounded-owned application payload slots, the Radio reassembly settings expected from that build, and explicit task-stack/other-composition reserves. The resulting `InboundDeliveryPool`, `ControlFramePool` and `ApplicationPayloadPool` are fixed arrays with generation-safe handles, explicit exhaustion and no heap fallback.

`MeshWholeDeviceMemoryAccounting` accepts that profile plus the concrete bounded security-composition owner and actual `RadioTransport` type. That owner must cover the provider, signer/registered-identity storage and pending-handshake records when separately composed. The accounting rejects a build when the profile's Radio values differ from the macros compiled into Radio, and reports one target-native total covering principal Mesh stores, clock and ACK state, all three owned pools, the complete Radio transport object, concrete security state, task stacks and other reserved composition storage. `RadioTransport::ReassemblyPayloadCapacityBytes` remains separately visible within that total for diagnostics; it is not double-counted.

## Selective multicast and Broadcast

Group and CapabilitySelector destinations are resolved once at the sender into a frozen bounded recipient set. Each resolved recipient receives an independent Node delivery with its own `MeshMessageId` and outcome state, while all recipient deliveries may share one immutable payload backing.

Broadcast is different: it is one bounded best-effort controlled flood with one `MeshMessageId`, no frozen recipient set and no promise of delivery to every member. Applications requiring per-member delivery knowledge use selective multicast rather than reliable Broadcast.

## Application delivery lifecycle

Selective application delivery deliberately keeps aggregate authority, direct-link evidence, forwarding acceptance and final destination acceptance separate.

`ApplicationTransmissionTable` and `ApplicationTransmissionCoordinator` own the sender-local bounded aggregate: the immutable deadline, frozen recipients, one independent `MeshMessageId` and outcome per recipient, and one shared immutable `ApplicationPayload`. Per-recipient routing, Radio correlation and acknowledgement machinery remains outside the aggregate and is reconciled through aggregate-aware coordinators.

Protection uses a composition-sized `MeshV1FrameWorkspace` with distinct inner/packet regions so a provider never has to support overlapping plaintext and ciphertext. Stable payloads seal directly from their immutable backing; repeatable sources materialize temporarily in the packet region before it is overwritten by the outer Hop frame. The serialized call clears both regions on every return. Mesh supplies no default workspace byte size; the concrete workspace belongs in the platform's whole-device composition reserve.

`ApplicationRadioSubmissionCoordinator` preflights the authoritative aggregate/message pair before submitting one recipient's next-hop work. Immediate deadline/permanent/attempt-limit stop conditions are committed to that exact recipient before composed delivery state is retired; retry and replan decisions leave the recipient pending.

`ApplicationRadioTerminalCoordinator` applies the same rule to deferred Radio evidence. Physical transmission completion and peer acknowledgement are direct-link facts only. They do not mean final destination delivery and do not by themselves consume the Mesh forwarding transition.

`ApplicationNextHopAcceptanceCoordinator` accepts only authenticated evidence for the exact expected next-hop device, membership incarnation and `MeshMessageId`. A valid next-hop acceptance commits exactly one forwarding transition and decrements `RemainingHopLimit` exactly once. The application recipient still remains pending because forwarding responsibility has merely moved to the next Mesh node. Wrong/stale evidence is non-mutating, and an already-terminal aggregate remains authoritative over late acceptance.

`ApplicationDeliveryAcknowledgementCoordinator` handles the distinct final-destination acknowledgement path. Its ACK means that the authenticated destination Mesh framework accepted the delivery; it does **not** imply that a requested Command/application operation completed successfully.

This separation prevents Radio submission, physical transmission, one-hop acknowledgement, Mesh next-hop acceptance and final destination delivery from being accidentally collapsed into the same success state.

## Controlled runtime reset

`MeshRuntimeResetCoordinator` provides deterministic local teardown for the principal non-application Mesh runtime stores: remote membership/liveness/tombstones, admission and probe reservations, inbound delivery reservations, direct-peer bindings, topology, route cache, pending destination acknowledgements, deferred Radio-terminal correlations, pairwise security sessions/provider secrets, clock observations and traffic reservations.

Shutdown ordering is explicit. Radio transports must first be stopped so provider callbacks cannot repopulate correlation state; application composition must then reset every exact per-recipient lifecycle and aggregate through `ApplicationRecipientLifecycleCoordinator`; the runtime reset coordinator can then clear the remaining Mesh state and reset the injected traffic governor last. This cleanup emits no wire cancellation and fabricates no delivery, Radio-terminal, membership, reachability or clock evidence.

Composition configuration is intentionally retained. Registered primitive receivers, injected policies and registered local Radio interfaces are not abandoned by a Mesh-service reset. Local `MembershipIncarnation`, `MeshMessageIdGenerator` continuity and incarnation-scoped `RadioIdentifier` allocation are managed by `LocalMeshIdentityLifecycleCoordinator`. Starting a genuinely new incarnation resets MessageId issuance and invalidates every local RadioIdentifier binding together, requiring Radio re-registration. Authenticated continuation preserves Radio bindings and accepts only an equal or advancing MessageId high-water mark; an older persistence snapshot is rejected rather than permitting identifier reuse.

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
