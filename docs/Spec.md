# SPEC — own_remote

> Living Source of Truth reconstructed from repository state on 2026-09-03.
> Statements not supported by current code, tests, notes, or Git history are marked `Unknown`.

## 1. Core Goal

The current project goal is the capability set already implemented in this repository: a Windows-native C++ remote desktop prototype in which a controlling client can:

- view the screen of a remote Windows machine;
- send mouse input to that machine;
- send keyboard input to that machine.

This goal is implemented over a direct TCP connection using the architecture documented below.

The approved next-stage engineering goal is to modernize the existing implementation to idiomatic modern C++ in small, independently verified steps while preserving observable behavior and preventing measurable performance regression. This is a maintainability and correctness refactor, not a feature expansion or transport-architecture rewrite.

Goal status:

- Current functional scope: implemented in code.
- Focused module tests: present and passing at the 2026-09-03 takeover checkpoint.
- Current-session end-to-end acceptance: `NOT VERIFIED`.
- Next-stage goal: staged modern C++ refactoring, approved on 2026-09-05.

Evidence: `client/client.cpp`, `client/client_head.hpp`, `RemoteControl/RemoteControl.cpp`, `RemoteControl/serve.hpp`, and the historical notes in `开发记录.text`.

## 2. Key Capabilities

Capabilities present in current code:

1. A server listens on TCP port `9988`, accepts one client, parses framed commands, captures the primary screen, and injects mouse/keyboard input.
2. A Win32 GUI client connects to an IPv4 address supplied on its command line, defaulting to `127.0.0.1`.
3. Screen transport uses a client-driven request/response loop capped at approximately 20 requests per second by `Sleep(50)`.
4. The server captures the desktop with GDI `BitBlt` into a 32-bit top-down DIB.
5. A `64x64` tile comparison finds one bounding dirty rectangle. If at least 40% of tiles or screen area is dirty, the update becomes a full frame.
6. Full and dirty frames are PNG-encoded with GDI+; unchanged frames carry metadata only.
7. The client validates screen-update metadata, decodes PNG with GDI+, maintains a full canvas, applies dirty patches, and paints with aspect-ratio-preserving scaling and double buffering.
8. The client maps local window mouse coordinates back to remote screen coordinates and throttles mouse-move messages to one per 50 ms.
9. TCP sends retry partial sends and serialize concurrent client sends through `SocketSender`.

## 3. Non-Goals

- Extending the implemented functional scope is outside the current refactoring goal.
- The repository contains a future architecture proposal mentioning server push, Desktop Duplication, H.264, Direct3D, hardware codecs, and adaptive streaming. These are reference ideas only, not approved goals or roadmap commitments.
- Multi-client service, authentication, encryption, NAT traversal, Internet relay, clipboard transfer, audio, file transfer, multi-monitor support, and production deployment are not part of the current implemented goal.
- Whether any item above should become a future goal remains `Unknown` and requires explicit human confirmation.
- The refactor must not change the TCP envelope bytes, command values, port, pull-based screen flow, screen-update semantics, dirty-region thresholds, input behavior, or visible rendering behavior unless a later Task explicitly changes that boundary.

## 4. Constraints

Confirmed constraints from code:

- Platform: Windows desktop APIs (`Win32`, Winsock 2, GDI, GDI+, `SendInput`, legacy `mouse_event`).
- Language: C++ with header-based shared utilities; current source uses Microsoft/MinGW-compatible Windows APIs.
- Network: IPv4 TCP, fixed server port `9988`, server backlog `1`, and one accepted connection per process execution.
- Wire framing: 12-byte packed header of three 32-bit integers: magic `0x55AA77CC`, command, body length.
- Screen protocol: packed fixed-width request/update structures; maximum dimension `16384`, maximum canvas `33,554,432` pixels, and maximum PNG payload just below 10 MiB.
- Current capture scope: the primary screen dimensions returned by `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`.
- Current client receive buffer: 10 MiB.
- Build system and supported compiler/version: `Unknown`; no CMake, Visual Studio solution, Makefile, or build instructions are tracked.
- Supported Windows versions, privilege model, security requirements, performance targets, and compatibility policy: `Unknown`.

## 5. Acceptance Dimensions

Dimensions supported by repository evidence, without inventing target values:

- Functional: remote screen display and mouse/keyboard control work between one server and one client.
- Protocol correctness: partial TCP sends, accumulated receives, screen message validation, and full/dirty/unchanged update semantics behave correctly.
- Visual correctness: initial/recovery full frame establishes a canvas; later dirty patches land at valid coordinates; display preserves aspect ratio.
- Responsiveness: network/capture work must not block the client UI message loop; mouse-move traffic is rate limited.
- Robustness: malformed or resource-exhausting screen metadata is rejected, and screen-response failure closes the connection rather than leaving the client blocked.
- Performance: desired FPS, latency, bandwidth, CPU, and GPU thresholds are `Unknown`.
- Security: acceptance requirements are `Unknown`; the current direct unauthenticated protocol must not be assumed production-safe.

## 6. Current Architecture

### 6.1 Components

- Server executable: console entry point in `RemoteControl/RemoteControl.cpp`; implementation in `RemoteControl/serve.hpp`.
- Client executable: Win32 GUI entry point in `client/client.cpp`; implementation in `client/client_head.hpp`.
- Shared transport helper: `socket_send.hpp`.
- Shared screen wire contract and validation: `screen_protocol.hpp`.
- Shared dirty-region analysis: `dirty_matrix.hpp`.
- Focused executable tests: `tests/socket_send_test.cpp`, `tests/screen_protocol_test.cpp`, and `tests/dirty_matrix_test.cpp`.

### 6.2 Runtime Data Flow

Screen path:

```text
Client screen thread
  -> CMD_SCREEN request (first/recovery request forces full frame)
  -> server TCP receive accumulator and packet parser
  -> server screen worker thread
  -> BitBlt BGRA capture
  -> 64x64 dirty-tile comparison
  -> unchanged metadata OR PNG full/dirty rectangle
  -> TCP response
  -> client validation and PNG decode
  -> full canvas replacement or dirty-patch composition
  -> InvalidateRect
  -> Win32 WM_PAINT aspect-ratio-preserving render
```

Input path:

```text
Win32 mouse/keyboard message
  -> coordinate conversion / event serialization
  -> CMD_MOUSE or CMD_KEYBOARD on the shared TCP connection
  -> server receive accumulator and dispatcher
  -> dedicated mouse or keyboard worker thread
  -> SetCursorPos + mouse_event, or SendInput
```

### 6.3 Concurrency Model

- Client UI thread owns the Win32 message loop.
- Client screen thread blocks on screen request/response traffic and updates the shared image under a `CRITICAL_SECTION`.
- Client UI and screen thread share one socket; `SocketSender` serializes sends.
- Server main thread owns accept/receive/parse/dispatch.
- Server has dedicated Windows message-loop threads for screen, mouse, and keyboard commands.
- Server screen state (`g_previous_screen` and dimensions) is owned in practice by the single screen worker.

## 7. Key Decisions

Only decisions demonstrable from code and Git history are recorded. Original alternatives and rationale are partly undocumented.

### Decision-001 — Explicit packet framing over TCP
- Decision: use a magic number, command, and body length with an accumulated receive buffer.
- Why: historical notes identify TCP stream coalescing and fragmentation as observed problems.
- Alternatives: `Unknown`.
- Why Not Alternatives: `Unknown`.
- Evidence / Context: `question.txt`; packet parsing in both client and server.

### Decision-002 — Keep blocking screen I/O off the UI thread
- Decision: use `SendScreenCallBack` as a separate client thread.
- Why: historical notes state that blocking `recv` on the UI thread freezes painting and input handling.
- Alternatives: `Unknown`.
- Why Not Alternatives: `Unknown`.
- Evidence / Context: `开发记录.text`, `question.txt`, `client/client.cpp`.

### Decision-003 — Serialize shared-socket sends and complete partial sends
- Decision: use `SendAll` and a mutex-owning `SocketSender`.
- Why: the Git history and tests record partial-send correctness and concurrent-send protection.
- Alternatives: separate connections or queues are discussed only as future suggestions; no decision is recorded.
- Why Not Alternatives: `Unknown`.
- Evidence / Context: commits `953d806` and `be28ff1`; `socket_send.hpp`; `tests/socket_send_test.cpp`.

### Decision-004 — Use tile-based dirty rectangle PNG updates
- Decision: compare 64x64 tiles, transmit one bounding rectangle, and fall back to full frame at a 40% threshold.
- Why: current code and latest commit aim to reduce transmission for low-change desktop scenes.
- Alternatives: multiple rectangles and H.264 are proposal-only.
- Why Not Alternatives: `Unknown`.
- Evidence / Context: commit `0a4192a`; `dirty_matrix.hpp`; `screen_protocol.hpp`.

### Decision-005 — Treat implemented capabilities as the current goal boundary
- Decision: the current Spec goal is the functionality already implemented; no next-stage goal is presently defined.
- Why: confirmed by the project owner on 2026-09-03.
- Alternatives: adopting one or more ideas from the architecture-upgrade proposal.
- Why Not Alternatives: no future direction has been selected or authorized.
- Evidence / Context: human checkpoint following initial Harness takeover and project reconstruction.

### Decision-006 — Modernize incrementally with behavior and performance gates
- Decision: replace C-style implementation patterns with idiomatic modern C++ through small MCU Tasks, showing each change at its checkpoint.
- Why: the project owner requested modern C++ style while requiring preserved functionality and no performance reduction.
- Alternatives: one large rewrite; cosmetic syntax-only replacement.
- Why Not Alternatives: a large rewrite makes regressions hard to isolate; cosmetic replacement does not address ownership and resource-safety problems.
- Evidence / Context: human approval on 2026-09-05.

## 8. Long-term Rules

Rules supported by current implementation and tests:

1. TCP reads and writes must not assume that one `send` corresponds to one `recv`.
2. Concurrent writes to the shared client socket must be serialized so packet bytes cannot interleave.
3. The client UI thread must not perform blocking network receive or screen decode work.
4. The first screen update, and recovery after an unapplied update, must request a full frame before dirty patches continue.
5. Screen update metadata must be validated before allocating or drawing image data.
6. An accepted screen request must receive an unchanged/full/dirty response or the connection must be closed to unblock the requester.
7. Shared image state must be synchronized between the client screen thread and UI thread.
8. Client and server must use the shared packet envelope and scoped command definitions from `packet_protocol.hpp`.
9. Packet header decoding from network bytes must use byte-safe copying rather than unaligned typed pointer dereferences.
10. Outgoing packets must remain one contiguous RAII-owned byte range so modernization does not add send-path payload copies.

## 9. Open Questions

The modern C++ refactoring goal is approved. The following facts remain unknown and do not authorize feature expansion:

1. What Windows versions, architectures, and compiler/toolchain must be supported?
2. What exact build and run commands are authoritative?
3. What manual end-to-end scenario is the canonical acceptance test for the implemented goal?
4. Should the existing checked-in executables, runtime logs, backup header, archive, and experimental capture sources remain versioned?
5. What future product goal, if any, should follow the refactoring work?
