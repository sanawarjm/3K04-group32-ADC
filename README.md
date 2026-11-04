# 3K04 Group 32 – Part A: Automated Dispensing Cabinet (ADC)

MECHTRON / SFWRENG 3K04, Fall 2026. Sub-team Part A: Sanawar Javaid, Samyar Seddighpour.

C++17 with CMake. The code is organised to match our Deliverable 1 architecture (D1 section 4):
one module per D1 component, each with a public header in `include/adc/` and its
implementation in `src/`.

## Layout

```
include/adc/   public interface of each component (one header per D1 component)
src/           implementation of each component, plus main.cpp
tests/         GoogleTest tests
docs/          traceability.md (D1 table, extended for D2) and diagrams/
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

## Working rules

- `main` stays buildable. Work on a branch (`feat/<component>`) and merge by pull request.
- Put requirement IDs in commit messages, e.g. `FR-DISP-006: block second dispense`.
- Keep the interface documentation in each header in sync with the code.
- Changes to `interface/` are agreed with Part B.
- Use fake demo credentials only; never store patient information.
