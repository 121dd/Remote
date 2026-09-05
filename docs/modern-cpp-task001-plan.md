# Packet Protocol Modernization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace duplicated C-style packet envelope allocation and parsing with one shared RAII-owned modern C++ implementation while preserving exact wire behavior and measured hot-path performance.

**Architecture:** Add a header-only shared `packet_protocol.hpp` because both executables are currently compiled from one translation unit and already consume shared header utilities. `PacketBuffer` owns one contiguous `std::vector<std::uint8_t>`; `ParsePacket` copies a 12-byte header safely, returns a status plus consumed-prefix/packet length, and copies a complete packet once into owned storage. Client and server retain their current receive loops and Win32 worker dispatch topology.

**Tech Stack:** C++17, MinGW GCC 16.2, Winsock 2, standard containers/algorithms/chrono, existing executable-style tests.

**Spec:** `docs/modern-cpp-refactor-design.md`

## Global Constraints

- Preserve packet magic `0x55AA77CC`, 12-byte header, command values `1`, `2`, `4`, and `2026`.
- Preserve screen, mouse, and keyboard body bytes.
- Preserve TCP port, pull flow, screen cadence, dirty-region behavior, rendering, input behavior, and server worker topology.
- Add no third-party dependency.
- Do not modify or revert the existing `RemoteControl/RemoteControl.exe` change or `client/client_head_backup.hpp` deletion.
- A complete outgoing packet remains one contiguous send buffer.
- Do not claim no performance regression unless repeated benchmark medians support it.

---

### Task 1: Capture baseline behavior and packet performance

**Files:**
- Create: `tests/packet_protocol_baseline_test.cpp`
- Create: `tests/packet_protocol_benchmark.cpp`
- Read: `client/client_head.hpp`
- Read: `RemoteControl/serve.hpp`

**Interfaces:**
- Consumes: current client `PackPacket`, `ParsePacket`, `GetPacketLen` behavior documented from source.
- Produces: byte-level compatibility cases and a benchmark output format used again after integration.

- [ ] **Step 1: Record clean source/build status without touching existing binary outputs**

Run the existing three tests and compile client/server to `%TEMP%`. Record compiler version, commands, exit codes, and current unrelated worktree changes in TASK 001.

- [ ] **Step 2: Add a standalone baseline compatibility test**

Implement local legacy packet helpers inside `tests/packet_protocol_baseline_test.cpp` and assert:

```cpp
static_assert(sizeof(LegacyPacketHeader) == 12);
Expect(serialized_bytes == ExpectedBytes(...), "packet bytes must match the legacy envelope");
Expect(ParseLegacy(partial_header).status == LegacyParseStatus::Incomplete,
       "partial header must remain incomplete");
Expect(ParseLegacy(two_packets).packet_length == first_packet_size,
       "parser must consume exactly one packet");
```

Include cases for zero body, leading garbage before magic, partial body, and two adjacent packets. The test captures behavior without including production packet declarations, avoiding duplicate symbol/type conflicts.

- [ ] **Step 3: Run the baseline compatibility test**

Run:

```powershell
g++ -std=c++17 tests/packet_protocol_baseline_test.cpp -o $env:TEMP/packet_protocol_baseline_test.exe
& $env:TEMP/packet_protocol_baseline_test.exe
```

Expected: PASS with `legacy packet baseline tests passed`.

- [ ] **Step 4: Add the benchmark harness**

Implement deterministic pack/parse loops in `tests/packet_protocol_benchmark.cpp` for body sizes `0`, `16`, `4096`, and `1048576`. Use a warm-up, at least seven samples, a fixed iteration count per size, `std::chrono::steady_clock`, median selection, and a checksum consumed after timing to prevent dead-code elimination. Emit one machine-readable line per case:

```text
legacy,size=4096,pack_ns=...,parse_ns=...,checksum=...
```

Compile with `-O2 -DNDEBUG`.

- [ ] **Step 5: Run and save the baseline benchmark evidence**

Run the benchmark at least three process invocations. Record all sample medians in TASK 001; do not commit generated executables or logs.

- [ ] **Step 6: CP1 report**

Show the two new test files, baseline results, build/test evidence, boundary status, and risks. Continue only after the checkpoint is visible to the user.

---

### Task 2: Implement the shared packet module test-first

**Files:**
- Create: `packet_protocol.hpp`
- Create: `tests/packet_protocol_test.cpp`

**Interfaces:**
- Produces:

```cpp
namespace remote {
inline constexpr std::int32_t kPacketMagic = 0x55AA77CC;
inline constexpr std::int32_t kMaxPacketBodyBytes = 10 * 1024 * 1024;

enum class Command : std::int32_t {
    Screen = 1,
    Mouse = 2,
    Keyboard = 4,
    Test = 2026,
};

#pragma pack(push, 1)
struct PacketHeader {
    std::int32_t magic;
    std::int32_t command;
    std::int32_t body_length;
};
#pragma pack(pop)

class PacketBuffer {
public:
    static PacketBuffer Build(Command command, const void* body, std::size_t body_length);
    static PacketBuffer CopyFrom(const std::uint8_t* packet, std::size_t packet_length);
    const PacketHeader& header() const noexcept;
    const std::uint8_t* body() const noexcept;
    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept;
private:
    std::vector<std::uint8_t> bytes_;
};

enum class ParseStatus { Incomplete, Complete, Invalid };

struct ParseResult {
    ParseStatus status{ParseStatus::Incomplete};
    std::size_t discarded_prefix{0};
    std::size_t packet_length{0};
    std::optional<PacketBuffer> packet;
};

ParseResult ParsePacket(const std::uint8_t* buffer, std::size_t length) noexcept;
}
```

- [ ] **Step 1: Write failing shared-module tests**

Test exact 12-byte header layout, exact command bytes, zero and nonzero bodies, all partial lengths, adjacent packets, leading garbage, negative body length, body length over `kMaxPacketBodyBytes`, null input, and exact `discarded_prefix`/`packet_length` values.

- [ ] **Step 2: Run tests to verify expected failure**

Compile `tests/packet_protocol_test.cpp` before creating the header. Expected: FAIL because `packet_protocol.hpp` does not exist.

- [ ] **Step 3: Implement minimal shared module**

Use `std::memcpy` for all header reads/writes; never dereference a reinterpreted header pointer from receive bytes. Use `std::vector<std::uint8_t>` for one contiguous allocation. Reject body sizes above `INT32_MAX` and `kMaxPacketBodyBytes`. Permit `{nullptr, 0}` bodies and reject null with nonzero length.

- [ ] **Step 4: Run shared-module and existing tests**

Expected: packet tests and all three existing focused tests pass.

- [ ] **Step 5: Run strict compile diagnostics**

Compile the packet test with `-std=c++17 -Wall -Wextra -Wpedantic -Werror`. Expected: no diagnostics in the new module/test.

---

### Task 3: Integrate the client without behavior changes

**Files:**
- Modify: `client/client_head.hpp`
- Modify: `client/client.cpp`
- Test: `tests/packet_protocol_test.cpp`

**Interfaces:**
- Consumes: `remote::PacketBuffer`, `remote::ParsePacket`, `remote::Command`.
- Preserves: `SendScreenCallBack`, `DOMOUSEACKTION`, and `winProc` observable behavior.

- [ ] **Step 1: Replace duplicated client declarations**

Include `../packet_protocol.hpp`; remove client `CMD`, `PACKET_MAGE`, `PacketHeader`, flexible-array `Packet`, deleter, `PacketPtr`, `GetPacketLen`, `PackPacket`, and `ParsePacket`.

- [ ] **Step 2: Adapt client sends**

Build `PacketBuffer` values and call:

```cpp
g_socket_sender.Send(
    g_connect_socket,
    reinterpret_cast<const char*>(packet.data()),
    static_cast<int>(packet.size()));
```

Keep the cast only at the Winsock `const char*` ABI boundary. Add a checked helper if packet size conversion could exceed `int`.

- [ ] **Step 3: Adapt client receive parsing**

Use the parse result's `packet_length` and `discarded_prefix` to remove exactly the bytes consumed from the accumulator. Preserve request timing and `force_full_next` behavior. Treat `Invalid` as connection failure rather than allocating or looping indefinitely.

- [ ] **Step 4: Compile and test client integration**

Build client to `%TEMP%`; run packet and all existing tests. Expected: all pass.

---

### Task 4: Integrate the server and preserve worker ownership

**Files:**
- Modify: `RemoteControl/serve.hpp`
- Modify: `RemoteControl/RemoteControl.cpp`
- Test: `tests/packet_protocol_test.cpp`

**Interfaces:**
- Consumes: shared packet module.
- Preserves: `HandleScreen`, `HandleMouse`, `HandleKeyboard`, `HandleCommand`, worker message IDs and thread topology.

- [ ] **Step 1: Replace duplicated server declarations**

Include `../packet_protocol.hpp`; remove server duplicate packet envelope, command enum, macro, allocation/deleter, packer, parser, and length function.

- [ ] **Step 2: Adapt handlers to typed packet access**

Pass `const remote::PacketBuffer&` or body pointer/length explicitly. Decode fixed body structures with `std::memcpy`. Do not change their serialized layouts in this MCU.

- [ ] **Step 3: Preserve `LPARAM` handoff ownership**

For asynchronous dispatch, allocate one `remote::PacketBuffer` owner with `std::make_unique`, release only at successful `PostThreadMessage`, reconstruct `std::unique_ptr<remote::PacketBuffer>` in the worker, and allow automatic destruction after handling. On post failure, retain automatic destruction.

- [ ] **Step 4: Adapt server response construction and main receive loop**

Use contiguous shared packets for screen responses. Consume parser-reported prefix plus packet length exactly; preserve processing of multiple accumulated packets.

- [ ] **Step 5: Compile and test server integration**

Build server to `%TEMP%`; build client again; run packet and all existing tests. Expected: all pass.

- [ ] **Step 6: CP2 report**

Show exact production diff, ownership before/after, protocol compatibility evidence, builds, tests, and boundary status.

---

### Task 5: Compare performance and close TASK 001

**Files:**
- Modify: `tests/packet_protocol_benchmark.cpp`
- Modify: `tasks/TASK_001_modernize_packet_protocol.md`
- Modify: `docs/CODEMAP.md`
- Modify: `docs/Spec.md` only if a durable new rule was discovered.

**Interfaces:**
- Consumes: legacy benchmark cases and shared packet API.
- Produces: comparable `modern,...` benchmark lines and completed acceptance evidence.

- [ ] **Step 1: Add modern implementation to the same benchmark harness**

Run legacy and modern cases in alternating order using identical payloads, iteration counts, compiler flags, and checksum strategy.

- [ ] **Step 2: Run repeated optimized benchmarks**

Run at least seven paired samples per body size. Compare median pack and parse time. Treat a modern median above baseline as failure; if variance overwhelms the difference, repeat and otherwise mark performance `NOT VERIFIED`.

- [ ] **Step 3: Run the complete automated safety net**

Compile and run four focused test executables plus both production targets. Confirm no new warnings in the shared packet module under strict diagnostics.

- [ ] **Step 4: Perform available manual end-to-end verification**

If both server and client can be safely launched locally, verify initial full frame, unchanged response continuity, dirty updates, mouse movement/click, and keyboard down/up. Otherwise record each item as `NOT VERIFIED`.

- [ ] **Step 5: Review the production diff for boundary violations**

Confirm there are no changes to screen constants, port, input enums/body layouts, capture/render calls, sleeps, thread count, or unrelated existing worktree changes.

- [ ] **Step 6: Recover project memory**

Fill TASK 001 execution, root cause, solution, files, verification, risks, and result. Update CODEMAP packet interfaces and ownership flow. Promote only durable verified rules to Spec.

- [ ] **Step 7: CP3 report**

Present acceptance-to-evidence mapping, benchmark table, unverified manual behavior, exact changed files, remaining risks, and proposed TASK 002. Do not begin TASK 002 without approval.
