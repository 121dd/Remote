# TASK 001 — Modernize Packet Protocol and Ownership

## 1. Goal

Replace duplicated C-style variable-sized packet allocation and parsing with one shared, contiguous, modern C++ packet representation while preserving the existing wire bytes and send/receive behavior.

## 2. Origin / Related Context
- Spec: `docs/Spec.md`, Decision-006
- Design: `docs/modern-cpp-refactor-design.md`
- Previous Task: None
- Bug / Issue: C-style flexible arrays, `malloc/free`, unsafe typed reads, and duplicated protocol code impede safe incremental modernization.

## 3. Scope

- Add a shared packet protocol/ownership module.
- Add packet contract, parsing, validation, and performance tests.
- Replace client/server duplicated packet envelope code with the shared module.
- Preserve existing accumulated-receive logic, dispatch behavior, and `SendAll` use.

## 4. Non-Scope

- Screen, mouse, or keyboard body-layout changes.
- Capture, PNG, rendering, dirty-region, input-injection, thread-topology, connection-model, or FPS changes.
- General Win32 RAII or global-state refactoring.
- Third-party dependencies.

## 5. Freedom

Within Scope, choose names and internal standard-library containers needed to provide contiguous storage, explicit ownership, safe parsing, and zero additional send-path payload copies. Do not cross Non-Scope.

## 6. Checkpoints

### CP1 — Baseline and failing contract tests
- Trigger: baseline builds/tests/packet benchmark recorded and new packet tests demonstrate missing target API.
- Report:
  - Current State
  - Completed
  - Boundary Status
  - Evidence
  - Risks
  - Next Step
  - Human Decision Needed?

### CP2 — Shared packet module integrated
- Trigger: both production targets use the shared module and all automated checks pass.
- Report:
  - Current State
  - Completed
  - Boundary Status
  - Evidence
  - Risks
  - Next Step
  - Human Decision Needed?

### CP3 — Acceptance
- Trigger: benchmark comparison and available end-to-end checks completed.
- Report includes exact diff summary and next MCU proposal.

## 7. Acceptance Criteria
- [ ] AC-01: Client and server packet-envelope declarations and implementations come from one shared module.
- [ ] AC-02: Packet storage uses standard RAII ownership and contains no flexible array member or `malloc/free`.
- [ ] AC-03: Header decoding avoids unaligned typed dereferences and rejects invalid lengths before allocation.
- [ ] AC-04: Packet bytes, command values, zero-body behavior, partial receive behavior, multi-packet behavior, and consumed lengths are covered by tests.
- [ ] AC-05: Existing three focused test executables pass.
- [ ] AC-06: Client and server build using their existing MinGW task parameters.
- [ ] AC-07: Packet benchmark demonstrates no median regression under the documented method, or the Task stops without claiming completion.
- [ ] AC-08: Existing unrelated worktree changes remain untouched.

## 8. Execution Notes

### CP1 — 2026-09-05

- Compiler: `g++.exe (GCC) 16.2.0`.
- Production builds were directed to `%TEMP%`; checked-in executables were not overwritten.
- Added `tests/packet_protocol_baseline_test.cpp` to lock the existing 12-byte little-endian envelope, zero-body behavior, incomplete prefixes, adjacent packets, and leading-garbage location.
- Added `tests/packet_protocol_benchmark.cpp` with deterministic payloads, warm-up, seven samples per case, median reporting, and checksum consumption.
- No production source was modified before CP1.

Baseline benchmark, nanoseconds per operation:

| Body bytes | Run 1 pack/parse | Run 2 pack/parse | Run 3 pack/parse |
|---:|---:|---:|---:|
| 0 | 52 / 5 | 52 / 5 | 57 / 5 |
| 16 | 54 / 5 | 52 / 5 | 51 / 4 |
| 4096 | 180 / 66 | 192 / 66 | 174 / 65 |
| 1048576 | 348718 / 109062 | 302187 / 99281 | 275135 / 94906 |

These values are baseline evidence only. Final comparison must run legacy and modern implementations in paired alternating order to reduce environmental bias.

### CP2 — 2026-09-05

- TDD RED: `tests/packet_protocol_test.cpp` failed to compile with `packet_protocol.hpp: No such file or directory`, confirming the test required the new production module.
- TDD GREEN: added header-only `packet_protocol.hpp`; strict `-Wall -Wextra -Wpedantic -Werror` packet tests pass.
- Client and server now share `remote::PacketHeader`, `remote::Command`, `remote::PacketBuffer`, `remote::ParseResult`, and `remote::ParsePacket`.
- Removed duplicated flexible-array packets, packet-specific `malloc/free`, custom deleters, unaligned integer dereferences, packers, parsers, and packet-length functions from both production sides.
- `PacketBuffer` owns one contiguous byte vector; header reads use `std::memcpy`; body sizes are validated before allocation.
- Server worker handoff retains the existing `PostThreadMessage` topology while transferring one heap-owned `PacketBuffer` with `std::unique_ptr`.
- Client/server builds were directed to `%TEMP%`; checked-in executables were not overwritten.

### CP3 — 2026-09-05

- The first modern benchmark using `std::vector(size)` showed material pack regression because the vector initialized the whole allocation before the payload copy.
- Replaced vector packet storage with RAII `std::unique_ptr<std::uint8_t[]>` plus an explicit size, avoiding the redundant full-buffer initialization while preserving contiguous bytes and the approved cached `header_`.
- Removed a second header decode/validation from the parse hot path by reusing the already validated local header.
- Final paired runs showed 4 KiB and 1 MiB pack/parse paths effectively at parity across scheduler variance. Small 0/16-byte construction retained an accepted 2–6 ns overhead from validation/ownership bookkeeping.
- Human accepted the small-packet overhead and instructed execution to continue.
- Fresh final verification built both production targets and ran all five test executables successfully.
- A controlled four-second loopback smoke test kept both processes running, connected over `127.0.0.1`, and emitted repeated 1920x1080 PNG frame logs with an empty server error log.
- Mouse and keyboard injection were not exercised during the hidden smoke test and remain `NOT VERIFIED`.

## 9. Problems / Root Cause

Pending execution.

## 10. Solution

Pending execution.

## 11. Files Changed

Pending execution.

## 12. Verification

| Acceptance | Evidence | Result |
|---|---|---|
| CP1 baseline builds | Client and server MinGW builds to `%TEMP%`, exit code 0 | PASS |
| CP1 existing tests | socket send, screen protocol, dirty matrix; all exit code 0 | PASS |
| CP1 packet characterization | `legacy packet baseline tests passed` under strict warnings-as-errors | PASS |
| CP1 performance baseline | Three optimized benchmark process runs recorded above | PASS |
| CP2 TDD RED | Missing shared header caused the expected compilation failure | PASS |
| CP2 shared module | Strict packet test output: `packet protocol tests passed` | PASS |
| CP2 production builds | Client and server MinGW builds to `%TEMP%`, exit code 0 | PASS |
| CP2 complete automated tests | Five test executables, all exit code 0 | PASS |
| AC-01 | Both production sides include and use `packet_protocol.hpp` | PASS |
| AC-02 | Packet path has no flexible array or packet-specific `malloc/free` | PASS |
| AC-03 | Header reads use `memcpy`; invalid lengths are tested and rejected | PASS |
| AC-04 | Exact bytes, zero body, partials, adjacent packets, prefix, and invalid lengths tested | PASS |
| AC-05 | Existing three focused tests pass at CP1 | PASS |
| AC-06 | Both production targets build after CP2 integration | PASS |
| AC-07 | Paired benchmark: large hot paths at parity; 2–6 ns small-packet overhead explicitly accepted by human | ACCEPTED |
| AC-08 | Existing binary modification and backup-header deletion preserved | PASS |
| End-to-end connection/screen | Four-second loopback: both processes alive, connection accepted, repeated frame logs, empty stderr | PASS |
| End-to-end mouse/keyboard | Hidden smoke test did not generate user input | NOT VERIFIED |

## 13. Known Risks / Limitations

- Win32 worker messages currently transfer packet pointers through `LPARAM`; integration must keep ownership explicit without expanding into a thread-architecture rewrite.
- Microbenchmark noise may require multiple runs and a conservative comparison.
- Automated tests cannot prove remote UI behavior; manual end-to-end verification may remain `NOT VERIFIED`.
- `PacketBuffer` intentionally caches `header_` in addition to the serialized header in owned bytes; this costs 12 bytes per live packet and was explicitly retained by human decision.
- Small control packets benchmark 2–6 ns slower to construct than the unsafe legacy baseline; this was explicitly accepted as immaterial relative to socket/system-call latency.

## 14. Knowledge Promotion
- [ ] No
- [x] Yes

If Yes, knowledge to promote into Living Spec:

- Shared packet envelopes must use one contiguous RAII-owned send buffer and safe `memcpy` header decoding.
- Performance checks must include representative large screen payloads; container value-initialization can create a measurable redundant memory pass.

## 15. Recovery
- [x] Task history updated
- [x] Codemap updated if needed
- [x] Living Spec updated if needed
- [ ] Git checkpoint created if appropriate

## 16. Result
- Status: COMPLETED
- Git Commit:
- Related Next Task: TASK 002 — Win32/Winsock RAII (proposed, not created)
