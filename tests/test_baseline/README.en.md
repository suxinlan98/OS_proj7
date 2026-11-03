# test_baseline

[中文](README.md) | [English](README.en.md)

## Purpose

Run the stock xv6-riscv rev5 regression tests on the modified system to confirm that the original process, memory, file-system, and syscall behavior has not regressed. Maps to contest task 3 (pass the bundled tests).

## Coverage

- All quick tests of `usertests -q` (60+ items covering processes, memory, file system, and syscalls).
- Both architectures: RISC-V and LoongArch64, CPUS=3, driven through the stock entry point `test-xv6.py`.
- The complete run (without `-q`, adding the six slow tests bigdir, manywrites, badwrite, execout, diskfull, outofinodes) is handled by `tests/full_usertests.sh`.

## Key assertions

- expect: output ends with `ALL TESTS PASSED`.
- reject: `panic:`.
- `run.sh` wraps the run in a 420 s timeout: when the guest panics, the stock script blocks on an empty output pipe forever, so the timeout turns that into a visible FAIL instead of a hang.

## Links

- Contest task 3.
- Full-run evidence: `tests/evidence/usertests-full-*.txt`.
