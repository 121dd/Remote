# CODEMAP

> Current code map reconstructed from repository state on 2026-09-03. Update when project structure, module responsibility, key interfaces, or major data flow changes.

## 1. Project Structure

```text
own_remote/
├── RemoteControl/
│   ├── RemoteControl.cpp       # server console entry point
│   ├── serve.hpp               # server networking, protocol dispatch, capture and input injection
│   └── RemoteControl.exe       # checked-in built artifact
├── client/
│   ├── client.cpp              # Win32 GUI client entry point
│   ├── client_head.hpp         # client networking, protocol, decode, render and input capture
│   ├── client_head_backup.hpp  # historical/backup implementation; not included by active entry point
│   └── client.exe              # checked-in built artifact
├── tests/
│   ├── dirty_matrix_test.cpp   # dirty-region unit tests
│   ├── screen_protocol_test.cpp# screen contract validation tests
│   ├── socket_send_test.cpp    # partial/concurrent send tests
│   ├── ScreenCapture*.cpp      # standalone capture experiments
│   ├── test.txt                # historical/experimental source-like text
│   └── server_runtime*.log     # captured runtime evidence
├── dirty_matrix.hpp            # tile comparison and full-frame fallback
├── packet_protocol.hpp         # shared packet envelope, contiguous ownership and safe parsing
├── screen_protocol.hpp         # packed screen wire structures and validator
├── socket_send.hpp             # complete-send loop and serialized sender
├── question.txt                # historical problem analysis
├── 开发记录.text               # development notes
├── 远程桌面流畅度优化与架构升级说明.txt # proposal/analysis, not current architecture
├── own_remote.zip              # untracked repository snapshot/archive; purpose unknown
└── AGENTS.md                   # Harness operating rules
```

No tracked build-system file is present.

## 2. Modules

| Module | Responsibility | Important State |
|---|---|---|
| `RemoteControl/RemoteControl.cpp` | Initialize console/DPI/GDI+/server, create workers, accept one connection, accumulate and dispatch packets, clean up | server process lifecycle |
| `RemoteControl/serve.hpp` | Server packet types/parser, capture and PNG encoding, screen diff state, input injection, worker loops, Winsock listener | `g_listen_socket`, `g_connect_socket`, previous screen buffer/dimensions, worker IDs |
| `client/client.cpp` | Initialize synchronization/GDI+/window/socket, connect to target IP, start screen worker, run UI message loop | client process lifecycle |
| `client/client_head.hpp` | Client packet types/parser, screen request/receive/decode/composition, painting, input mapping/transmission, Winsock setup | shared socket/sender, window, canvas, remote dimensions, image lock |
| `packet_protocol.hpp` | Shared fixed-width command/envelope contract, contiguous RAII packet storage, safe parsing and length validation | no global mutable state |
| `socket_send.hpp` | Retry partial TCP sends; optionally serialize all sends from one `SocketSender` | per-sender mutex |
| `screen_protocol.hpp` | Define and validate full/dirty/unchanged screen messages and allocation limits | no mutable state |
| `dirty_matrix.hpp` | Compare BGRA frames by tiles and produce one bounded dirty region or full-frame fallback | no mutable state |

## 3. Key Entry Points

- Server: `main()` in `RemoteControl/RemoteControl.cpp`.
- Client: `WinMain()` in `client/client.cpp`.
- Client window procedure: `winProc()` in `client/client_head.hpp`.
- Client screen loop: `SendScreenCallBack()` in `client/client_head.hpp`.
- Server command dispatch: `HandleCommand()` in `RemoteControl/serve.hpp`.
- Server screen work: `HandleScreenThreadFuc()` -> `HandleScreen()`.
- Server input work: `HandleMouseThreadFuc()` -> `HandleMouse()` and `HandleKeyboardThreadFuc()` -> `HandleKeyboard()`.

## 4. Key Interfaces

### 4.1 Packet Envelope

`remote::PacketHeader` is shared by client and server and packed to one-byte alignment:

```text
int32 magic = 0x55AA77CC
int32 cmd
int32 body_len
body_len bytes of body
```

`remote::Command` values: screen `1`, mouse `2`, keyboard `4`, test `2026`.

`remote::PacketBuffer` owns one contiguous serialized packet through RAII storage. `remote::ParsePacket` reads headers with `std::memcpy`, reports incomplete/complete/invalid status, and identifies discarded prefixes plus exact packet length.

### 4.2 Screen Protocol

- `ScreenRequest { int32 force_full }`
- `ScreenUpdateHeader` (32 bytes) with frame type, remote dimensions, rectangle, and image length.
- Frame types: unchanged `0`, full `1`, dirty `2`.
- `IsValidScreenUpdate(...)` enforces length, geometry, and resource limits.

### 4.3 Input Protocol

- Mouse body: native-layout `Mouse { int action; POINT ptXY; }`.
- Keyboard body: native-layout `Keyboard { int virtual_code; int key_state; }`.
- These structs are duplicated between client and server rather than defined in one shared contract.

### 4.4 Transport Helpers

- `SendAll(socket, data, length, send_function)` loops until all bytes are sent or an error/zero occurs.
- `SocketSender::Send(...)` places a mutex around `SendAll` for shared-socket use.

## 5. Data Flow

### Screen

```text
WinMain starts SendScreenCallBack
  -> force-full CMD_SCREEN request
  -> server main recv/ParsePacket/HandleCommand
  -> WM_HANDEL_SCREEN
  -> BitBlt capture
  -> FindDirtyRegion(previous, current, 64, 40)
  -> GDI+ PNG encode when changed
  -> ScreenUpdateHeader + optional PNG response
  -> client recv/ParsePacket/IsValidScreenUpdate
  -> GDI+ decode
  -> replace full canvas or draw dirty patch
  -> InvalidateRect/WM_PAINT
  -> Sleep(50), then next request
```

### Mouse and Keyboard

```text
Win32 input message
  -> serialize CMD_MOUSE/CMD_KEYBOARD
  -> SocketSender on shared TCP socket
  -> server parse/dispatch to worker message queue
  -> SetCursorPos + mouse_event / SendInput
```

### Ownership and Synchronization

- `remote::PacketBuffer` owns packet bytes with `std::unique_ptr<std::uint8_t[]>`; its send range is exactly `data()` plus `size()`.
- Server dispatch transfers a heap-owned `PacketBuffer` to a worker through `PostThreadMessage`; `std::unique_ptr` destroys it on post failure or after worker handling.
- Client `g_image` and remote dimensions are guarded by `g_cri_sec`.
- Client socket sends are guarded by `g_socket_sender`; server replies originate from the screen worker in current code.

## 6. Dependencies

Confirmed platform/library dependencies:

- Windows SDK / Win32 user and graphics APIs.
- Winsock 2 (`ws2_32`).
- GDI and GDI+ for capture, PNG codec, decode, composition, and drawing.
- IP Helper API for enumerating server IPv4 addresses.
- COM stream/global-memory APIs used by GDI+ image encoding/decoding.
- C++ standard library: vectors, smart pointers, mutexes, atomics, threads, algorithms, integer types.

Compiler and linker invocation, exact language standard, and dependency version policy are `Unknown` because no build manifest is tracked.

## 7. Tests and Verification Assets

- `tests/socket_send_test.cpp`: partial-send completion, failure propagation, concurrent serialization.
- `tests/packet_protocol_test.cpp`: shared packet bytes, parsing states, malformed lengths, prefix and ownership behavior.
- `tests/packet_protocol_baseline_test.cpp`: legacy wire/receive characterization.
- `tests/packet_protocol_benchmark.cpp`: paired legacy/modern packet performance measurement.
- `tests/screen_protocol_test.cpp`: valid full/dirty/unchanged messages and malformed/resource-exhaustion rejection.
- `tests/dirty_matrix_test.cpp`: unchanged, one tile, edge clamping, threshold fallback, and first-frame behavior.
- `tests/server_runtime.log`: prior runtime capture output; historical evidence only, not an automated test.
- `tests/ScreenCapture.cpp`, `tests/ScreenCapture_gdiplus.cpp`, `tests/test.txt`: experiments or historical aids; not known to be part of a test suite.

## 8. Related Tasks and Git History

No Harness Task history existed at takeover time.

- `tasks/TASK_001_modernize_packet_protocol.md`: shared modern packet protocol and ownership migration.

Relevant recent commits:

- `0a4192a` — dirty matrix, screen protocol validation, tests, and full/dirty screen updates.
- `be28ff1` — complete-send behavior and send locking with tests.
- `8f129c8` — variable-sized packet ownership moved toward smart pointers.
- `97aeedd` — mouse throttling and server-side command workers.
- `62d7170` — repository history describes completion of the basic feature set.

Future Task files should link affected modules and commits here when structure or responsibility changes.
