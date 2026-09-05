# Modern C++ Refactor Design

## Goal

Modernize the current Windows remote desktop implementation to idiomatic modern C++ in small, reviewable steps while preserving the implemented feature set, wire compatibility, and runtime performance.

## Scope

- Packet representation, serialization, parsing, and ownership.
- Win32, Winsock, GDI/GDI+, COM, and thread-handle lifetime management.
- Client/server duplicated protocol declarations.
- Global state and synchronization where ownership can be clarified without changing runtime behavior.
- C-style casts, macros used as constants, `NULL`, manual allocation, and dead commented-out implementation code in files touched by an MCU.
- Focused unit, integration, build, and performance-regression checks.

## Non-Scope

- New remote-control features.
- Server push, H.264, Desktop Duplication, Direct3D rendering, multiple clients, multiple monitors, authentication, encryption, NAT traversal, or new dependencies.
- Changes to TCP port `9988`, command values, packet envelope bytes, screen update wire layout, pull cadence, tile size, full-frame threshold, input mapping, or visible rendering behavior.
- Unrelated cleanup of experiments, archives, logs, binaries, or existing user worktree changes.

## Chosen Approach

Use staged, behavior-preserving modernization. Each MCU begins with tests or a measurable baseline, makes one cohesive ownership/interface change, builds both executables, runs all relevant tests, compares performance where the hot path is affected, reports the diff and evidence, and stops at a human checkpoint.

## MCU Sequence

1. Unify and modernize packet protocol and packet memory ownership.
2. Introduce RAII wrappers for Winsock and packet-independent Win32 handles/resources.
3. Modernize client image ownership and synchronization.
4. Modernize server capture/encoding resource ownership.
5. Encapsulate client and server runtime global state without changing thread topology.
6. Remove remaining C-style casts, macros-as-constants, `NULL`, and dead code within the active production paths.
7. Run full regression, performance comparison, and project-memory recovery.

Each later MCU requires the preceding checkpoint to be accepted; the sequence may be rerouted if evidence reveals a dependency or regression.

## MCU 1 Design: Packet Protocol and Ownership

### Current Problem

Client and server duplicate `PacketHeader`, `Packet`, command enums, parser, packer, deleter, and packet-length logic. Variable-sized packets use a non-standard flexible array member plus `malloc/free`, and parsing uses direct pointer reinterpretation that can perform unaligned reads. This mixes wire representation, allocation, ownership, and parsing.

### Target Design

Create one shared packet module with:

- fixed-width packed `PacketHeader` and compile-time size assertion;
- scoped `Command` values matching the current integer values;
- an owning `PacketBuffer` backed by one contiguous `std::vector<std::uint8_t>` so a packet remains a single send buffer without extra payload copies after construction;
- safe header decoding through `std::memcpy`, avoiding unaligned typed pointer dereferences;
- a parser result that distinguishes incomplete data from a complete packet and reports the exact consumed byte count;
- validation of negative and oversized body lengths before allocation;
- client and server adapters changed to consume the shared representation without changing emitted bytes.

The accumulated TCP receive buffer and its current processing order remain unchanged in this MCU.

## Compatibility Contract

- `PacketHeader` remains exactly 12 bytes.
- Magic remains `0x55AA77CC`.
- Commands remain screen `1`, mouse `2`, keyboard `4`, test `2026`.
- Serialized screen/input bodies remain byte-for-byte compatible.
- Each outgoing packet remains one contiguous memory range passed to `SendAll`.
- Partial TCP receives and multiple packets in one receive buffer remain supported.

## Performance Contract

- Establish a packet benchmark before implementation covering small input packets and representative screen payloads.
- Compare baseline and refactored median time using the same compiler, optimization level, payloads, iteration count, and host.
- Refactored median pack/parse time must not exceed baseline median in repeated runs; if measurement noise prevents a conclusion, report `NOT VERIFIED` rather than claiming no regression.
- No new payload copy may be added to the send path beyond the existing packet construction copy.

## Error Handling

- Incomplete data returns an incomplete parse result without allocation.
- Invalid negative or oversized lengths are rejected explicitly rather than treated as incomplete forever.
- Allocation is managed by standard containers; exception behavior remains process-level unless a later Task defines recovery policy.
- Worker-message ownership transfer must remain explicit and leak-free; if the new representation cannot cross `LPARAM` safely without changing architecture, MCU 1 will retain a small owning handoff object rather than redesigning worker queues.

## Verification

- Existing socket-send, screen-protocol, and dirty-matrix tests pass.
- New packet tests cover exact bytes, zero-length bodies, partial headers, partial bodies, adjacent packets, leading garbage before magic, negative length, excessive length, and consumed length.
- Client and server build successfully with the existing VS Code/MinGW commands.
- Packet benchmark satisfies the performance contract or the Task stops at an Emergency Checkpoint.
- Manual end-to-end screen/input verification is performed if the environment permits; otherwise it is `NOT VERIFIED`.

## Checkpoint Reporting

At every MCU checkpoint, report:

- exact files changed;
- concise before/after interface and ownership model;
- boundary status;
- build/test/benchmark evidence;
- functional items not verified;
- risks and next proposed MCU;
- a readable diff summary for human review.
