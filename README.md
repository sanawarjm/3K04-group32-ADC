# 3K04 Group 32 – Part A: Automated Dispensing Cabinet (ADC)

MECHTRON / SFWRENG 3K04, Fall 2026. Sub-team Part A: Sanawar Javaid, Samyar Seddighpour.

C++17 with CMake. The code is organised to match our Deliverable 1 architecture (D1 section 4):
one module per D1 component, each with a public header in `include/adc/` and its
implementation in `src/`.

## Layout

```
include/adc/   public interface of each component (one header per D1 component), plus the
               D2 support modules (see docs/module-guide.md)
src/           implementation of each component, plus main.cpp
tests/         GoogleTest tests
docs/          traceability.md (D1 table, extended for D2), module-guide.md, specification.md, diagrams/
interface/     ADC ↔ Monitoring System interface, to agree with Part B
```

## D1 components → code

| D1 component | Header | Class | D1 user stories |
|---|---|---|---|
| Access Control Manager | `include/adc/access_control_manager.hpp` | `AccessControlManager` | US-AUTH-01, US-SEC-01 |
| Dispensing Controller | `include/adc/dispensing_controller.hpp` | `DispensingController` | US-DISP-01, US-SAF-01 |
| Inventory Manager | `include/adc/inventory_manager.hpp` | `InventoryManager` | US-INV-01, US-REL-01 |
| Event Logger | `include/adc/event_logger.hpp` | `EventLogger` | US-LOG-01, US-AUD-01 |
| Fault Detector | `include/adc/fault_detector.hpp` | `FaultDetector` | US-FAULT-01 |
| Operational Mode Manager | `include/adc/operational_mode_manager.hpp` | `OperationalModeManager` | US-MODE-01 |
| Communication Manager | `include/adc/communication_manager.hpp` | `CommunicationManager` | US-COM-01, US-AVL-01 |
| Cabinet hardware (D1 "Hardware" swimlane) | `include/adc/hardware_interface.hpp` | `HardwareInterface` | — |

Each header lists the D1 requirements traced to that component. The full table is in
[`docs/traceability.md`](docs/traceability.md).

D1 behaviour models and where they will live:

| D1 model | Component |
|---|---|
| State Machine 1 – Operational Modes | Operational Mode Manager |
| State Machine 2 – Dispensing Lifecycle | Dispensing Controller |
| State Machine 3 – Monitoring System Communication | Communication Manager |
| Activity: Authenticate and dispense medication | Access Control Manager → Dispensing Controller |
| Activity: Unsuccessful or interrupted dispense → Fault mode | Dispensing Controller, Fault Detector, Operational Mode Manager |
| Activity: Loss and restoration of Monitoring System communication | Communication Manager |
| Activity: Replenish medication | Access Control Manager → Inventory Manager |

## Build, run, test

Needs CMake 3.16+ and a C++17 compiler (Visual Studio 2022 "Desktop development with C++",
MinGW-w64, GCC or Clang). The first configure downloads GoogleTest.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the program:

- Visual Studio (Windows): `build\Debug\adc_app.exe` (and use `ctest --test-dir build -C Debug`)
- Other generators: `./build/adc_app`

### Windows (MSYS2 UCRT64 GCC, PowerShell)

Put `C:\msys64\ucrt64\bin` on `PATH` first (VS Code CMake Tools does this for you).

```powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
cmake -S . -B build -G Ninja          # Ninja ships with MSYS2 UCRT64 (VS Code uses it)
cmake --build build
ctest --test-dir build --output-on-failure
build\adc_app.exe --reset             # fresh demo data in .\data
```

`-G "MinGW Makefiles"` works the same way (`cmake -S . -B build -G "MinGW Makefiles"`, then
`cmake --build build`, `build\adc_app.exe`, `ctest --test-dir build --output-on-failure`), but it
needs `mingw32-make`, which MSYS2 does not install by default:
`pacman -S mingw-w64-ucrt-x86_64-make` in an MSYS2 shell.

`adc_app` options: `--data-dir <folder>` (default `data`), `--reset` (deletes only the ADC's own
`.tsv` files in that folder first), `--session-timeout <seconds>` (default 60).

## Module guide and specification

- [`docs/module-guide.md`](docs/module-guide.md): module table (MG numbers and secrets), uses
  hierarchy, listeners, control loop. The seven D1 components are unchanged; the new modules
  are hardware-hiding and software-decision-hiding modules added for D2.
- [`docs/specification.md`](docs/specification.md): 4-variable model, every function table,
  the three state-machine transition tables, contracts, invariants, D1 clarifications C1–C11.
- [`docs/traceability.md`](docs/traceability.md): all 59 D1 requirements → D2 spec → code → test.
- [`interface/adc-monitoring-interface.md`](interface/adc-monitoring-interface.md): ADC ↔
  Monitoring System messages (Part A proposal, pending Part B agreement).

## Demo accounts (fake, demo only)

| Badge | PIN | Name | Role |
|---|---|---|---|
| N100 | 1234 | Nurse Alex Chen | Nurse |
| N101 | 4321 | Nurse Jordan Lee | Nurse |
| P200 | 2345 | Pharmacist Priya Shah | Pharmacist |
| T300 | 3456 | Technician Tom Reyes | Technician |

The code stores only salted hashes of these PINs. No PIN is ever logged, sent or shown.

Demo drug library (seeded on the first run): M01 Acetaminophen 500 mg tablet (D1, 20),
M02 Ondansetron 4 mg tablet (D2, 15), M03 Morphine 2 mg/mL injection (D3, 10),
M04 Heparin 5,000 units/mL injection (D4, 12).

## Menu

Every option is always shown, so denials can be demonstrated. Options 10 and 11 are a
**test harness**: they change the "physical world" (the simulated cabinet and the stand-in
Monitoring System). The ADC only sees those changes through its sensors and its link.

| Key | Option | D1 use case / requirements |
|---|---|---|
| 1 | Sign in | Authenticate User |
| 2 | Sign out | |
| 3 | View inventory | View Inventory, FR-INV-001 |
| 4 | Request medication | Request/Dispense Medication |
| 5 | Replenish medication | Replenish Inventory |
| 6 | Cancel current request | SM2 cancelRequest |
| 7 | View event log | NFR-AUD-*, NFR-SEC-002 |
| 8 | Maintenance: 1 enter / 2 exit / 3 run diagnostics | Perform Maintenance, Manage Operational Mode |
| 9 | Clear fault | Acknowledge/Clear Fault |
| 10 | [Simulated cabinet] 1 open / 2 close / 3 take N / 4 add N / 5 force open / 6 jam lock / 7 repair lock / 8 fail sensors / 9 repair sensors / 10 show drawers | hardware swimlane |
| 11 | [Simulated network] 1 Monitoring System offline / 2 online / 3 show what it received / 4 send a config update | SM3, C8 |
| 12 | Restart ADC (rebuild from storage, sign in again) | NFR-REL-001, NFR-REL-004 |
| 0 | Exit | |

Press Enter on an empty line to redraw the status bar.

## Demo scenarios (menu keys)

Run `build\adc_app.exe --reset` and type the keys in order. "10/1 D3" means option 10,
action 1, drawer D3. The same scenarios also run automatically in `tests/test_scenarios.cpp`.

| # | Scenario | D1 / requirements | Keys | What you see |
|---|---|---|---|---|
| 1 | Authenticate → dispense → log | Activity 1; US-AUTH-01, US-DISP-01, US-LOG-01 | 1 N100 1234 · 4 M03 2 · 10/1 D3 · 10/3 D3 2 · 3 · 10/2 D3 · 1 P200 2345 · 7 · 11/3 | only D3 unlocks; M03 goes 10 → 8 before closing; `[OK] Dispense complete`; log has AUTH_SUCCESS, INVENTORY_CHANGED, DISPENSE_OUTCOME COMPLETED; the Monitoring System received them |
| 2 | Invalid credentials | US-AUTH-01 AC-03 | 1 N100 9999 | `[ERROR] Sign-in failed: badge or PIN not recognised.`; AUTH_FAILURE with the badge only |
| 3 | Permission denied | US-SEC-01 AC-02, US-MODE-01 AC-03 | 1 N100 1234 · 5 · 7 · 8/1 | `[ERROR] You are not authorised to ...` three times; ACCESS_DENIED logged |
| 4 | Unavailable medication | US-DISP-01 AC-02 | 4 M03 999 | `[ERROR] Not enough Morphine ...`; DISPENSE_OUTCOME REJECTED |
| 5 | Hardware fault → Fault → clear | Activity 2; US-FAULT-01 AC-02, US-SAF-01, FR-MODE-005, NFR-SAF-002 | 4 M03 2 · 10/6 D3 · 10/1 D3 · 10/3 D3 2 · 10/2 D3 · (wait 2 s, Enter) · 4 M01 1 · 9 · 1 T300 3456 · 9 · 10/7 D3 · 9 | `!!! WARNING: DISPENSING DISABLED - SAFETY FAULT !!!`, FAULT + buzzer, alarm sent; dispensing refused; nurse clear denied; technician clear denied while jammed (FAULT_CLEAR_DENIED); repair, then clear → NORMAL |
| 6 | Incomplete dispense | FR-DISP-007, FR-FAULT-004, C5 | 1 N100 1234 · 4 M03 1 · 10/1 D3 · (wait 30 s) · 10/2 D3 · 1 T300 3456 · 9 | `Dispense timed out`; IncompleteDispense → FAULT; cleared by the technician |
| 7 | Interrupted, no second dose | NFR-SAF-003, US-SAF-01 AC-03 | 1 N100 1234 · 4 M03 2 · 10/1 D3 · 10/8 D3 · 10/2 D3 · 4 M03 2 · 10/9 D3 · 1 T300 3456 · 9 · 1 N100 1234 · 4 M03 1 | `Dispense interrupted`; the drawer locks on close; the next request is refused until a technician clears the fault; only a new request unlocks again |
| 8 | Communication loss | Activity 3; US-COM-01 AC-02, US-AVL-01, NFR-PRF-003 | 11/1 · (Enter a few times) · 4 M03 1 · 10/1 D3 · 10/3 D3 1 · 10/2 D3 | within 5 s `Link: DISCONNECTED` and `WARNING: Monitoring link down`; mode stays NORMAL; dispensing works; the Unsent count grows |
| 9 | Restoration | FR-COM-005, US-COM-01 AC-03 | 11/2 · (Enter) · 11/3 | `DISCONNECTED -> RESYNCHRONIZING -> CONNECTED`; Unsent 0; the received list has every event in order, 0 duplicates |
| 10 | Replenish | Replenish activity; FR-INV-003 | 1 P200 2345 · 5 M04 · 10/1 D4 · 10/4 D4 8 · 10/2 D4 · 3 | M04 goes 12 → 20; REPLENISH_OUTCOME COMPLETED; inventory sent |
| 11 | Maintenance | FR-MODE-003/004 | 1 T300 3456 · 8/1 · 8/3 · 1 N100 1234 · 4 M03 1 · 1 T300 3456 · 8/2 | MAINTENANCE (amber); diagnostics pass; the nurse's dispense is refused; back to NORMAL |
| 12 | Restart | NFR-REL-001, NFR-REL-004, US-REL-01 | 12 · 1 P200 2345 · 3 · 7 · 10/5 D2 · 12 | signed out after the restart; inventory and log unchanged; forced-open drawer → self-test fails → FAULT |

## Working rules

- `main` stays buildable. Work on a branch (`feat/<component>`) and merge by pull request.
- Put requirement IDs in commit messages, e.g. `FR-DISP-006: block second dispense`.
- Keep the interface documentation in each header in sync with the code.
- Changes to `interface/` are agreed with Part B.
- Use fake demo credentials only; never store patient information.
