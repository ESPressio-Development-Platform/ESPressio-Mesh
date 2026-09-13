# Tranche 8 Closure Report

Date: 2026-09-13
Branch: `primitives_redesign`
Status: **CLOSED**

## Scope

This report records the implementation and validation evidence for Primitive Platform Redesign Tranche 8 as it applies to `ESPressio-Mesh` and its neutral integration boundary.

The authoritative Primitive Platform Redesign architecture remains the source of truth for locked ownership, lifecycle, evidence, dependency and release rules. This report does not redefine those contracts.

## Locked ownership confirmed

- A2 owns logical Primitive-family pursuit/retry after synchronous ownership handoff.
- Mesh owns authenticated membership, topology, route selection, forwarding, Broadcast dissemination and Mesh application lifecycle.
- Radio/R3 owns physical fragmentation, reassembly and arbitration for one logical transfer.
- Timing owns clock estimation, filtering, discipline, uncertainty, reliability and adaptive synchronization behavior.
- Mesh owns only clock root/parent/reference topology selection and passes the selected direct-reference relationship to a fixed transport control surface.
- Primitive family runtimes alone decide the exact M1 admission result.

## M8-21 / M8-22 neutral and family integration

The replacement family integration is implemented in `ESPressio-MeshAdapters` over the neutral Mesh/A2 boundaries. Event, Command and State no longer require Event-only Mesh transport/submission runtimes.

Final MeshAdapters integration checkpoint:
- commit `ce0a745498216248279aafc70d6603e49b10a6ab` — `Record Tranche 8 final integration gate`
- combined contracts workflow `34753328372` — SUCCESS
- State outbound A2 workflow `34753328259` — SUCCESS
- Command recovered-response A2 workflow `34753328297` — SUCCESS

Those exact final runs resolved the live finalized `ESPressio-Mesh/primitives_redesign` branch and the live `ESPressio-Adapters/primitives_redesign` branch, proving the Event/Command/State replacement contracts against the completed Mesh M8-23/M8-24 state.

They cover neutral ingress/lower transport, Event ingress/outbound and no re-egress, Command ingress/idempotency/local egress/terminal request-delivery failure/recovered response, State ingress/outbound convergence feedback, opaque route-token/provenance rules and predecessor Event-path removal.

## M8-23 evidence hardening

Exact Mesh implementation checkpoint `5f36b51f6ce570e97e4f95f975f039d61e2287cd` is green in:
- Tranche 8 Mesh closure workflow `34753038491` — SUCCESS
- Mesh redesign contracts workflow `34753038532` — SUCCESS
- Mesh clock and runtime redesign workflow `34753038454` — SUCCESS

The closure workflow proves:

1. Mesh core dependency boundary and unchanged manifest version `1.0.0`.
2. Absence of predecessor worker mechanisms after comments are removed from guard input.
3. Exact seven-disposition M1 receiver admission and evidence semantics.
4. Complete relay record + byte + workspace ownership before responsibility acceptance.
5. Relay membership/capacity compatibility.
6. Finite and non-increasing remaining residence.
7. Forward-once Broadcast lifecycle with bounded `DeferredLocal` retry and no re-fan.
8. Family-neutral Broadcast policy restrictions.
9. Source Broadcast no-feedback / no own-family runtime loop.
10. Logical Mesh-to-Radio transfer handoff without physical-fragment ownership leakage.
11. Mesh v1 authentication/replay/security contract.
12. Deterministic Mesh v1 codec fuzz coverage.
13. Three-node A -> B -> C forward-once behavior, including authenticated original-source membership at C without inventing an A-C direct link/session.
14. Clock ownership and conservative source/failover behavior.
15. Generic Thread runtime-worker behavior.
16. Deterministic Mesh-owned memory/resource accounting.

The three-node fixture intentionally distinguishes authenticated Mesh membership from direct-neighbour reachability: C knows A as an Active authenticated Mesh member but receives the protected Broadcast only from direct sender B.

## M8-24 documentation and resource accounting

Following the fully green M8-23 implementation checkpoint:

- `.github/workflows/mesh-runtime-redesign.yml` was corrected so predecessor-code guards strip comments before testing executable-code patterns; prohibited runtime mechanisms remain prohibited.
- `tests/mesh_memory_accounting_test.cpp` no longer imports deleted Radio reassembly/logical-transfer macros. Radio-owned R3 resources remain Radio-owned and separately published; Mesh accounting covers Mesh-owned state only.
- `README.md` was rewritten against current `primitives_redesign` contracts: neutral Primitive-family boundary, exact admission evidence, Broadcast semantics, generic Thread worker, current clock ownership, Mesh-only resource accounting and current dependency boundaries.
- `library.json` remains version `1.0.0` and depends only on `ESPressio-System`, `ESPressio-Threads`, `ESPressio-Primitive`, `ESPressio-Radio`, `ESPressio-Timing` and `ESPressio-Security`, all on `primitives_redesign`.
- No compatibility shim or predecessor Event-only runtime was reintroduced.

## Adapters native-CI caveat

The final successful MeshAdapters integration uses the live `ESPressio-Adapters/primitives_redesign` tip `b8a17228bb3d5e87ae622dbab782a308326bf543` containing the mixed-policy evidence correction.

Its native workflow `34748737223` has remained queued without job allocation. This is recorded as an infrastructure/native-runner caveat, not an integration failure: the exact Adapters tip is compiled and exercised successfully by the final MeshAdapters integration workflows above.

## Closure decision

All locked Tranche-8 structural implementation and validation gates are satisfied. Tranche 8 is therefore **CLOSED**.

This closure authorizes continuation to structural Tranche 9 under the existing implementation authorization. It does not authorize release work.

## Release boundary

Tranche 8 closure does not authorize version changes, release/CHANGELOG finalization, `main` reintegration, tags, releases or Wiki publication.
