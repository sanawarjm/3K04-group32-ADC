# Module guide (Part A ADC, Deliverable 2)

The seven D1 components (D1 section 4) are unchanged: same names, classes and files. The
modules marked **new** are hardware-hiding and software-decision-hiding modules added for D2.
Each one hides one anticipated change ("decompose by secret, not by process step", Week 5).
Every header in `include/adc/` starts with its D1 comment, then a Module Interface
Specification block, then one contract block above each public method (Week 6).

## Module table

| MG # | Module | Responsibility | Secret (anticipated change it hides) | Files | New? |
|---|---|---|---|---|---|
| 1 | ADC application | Builds every module, wires the listeners, runs the control loop and the menu | none of its own: wiring and the run loop only (composition root) | `src/main.cpp`, `include/adc/adc_system.hpp`, `src/adc_system.cpp` | new |
| **1.1** | **Hardware-hiding** | | | | |
| 1.1.1 | HardwareInterface (abstract) | The software inputs `i_*` and outputs `o_*` of the 4-variable model | how drawers, locks, sensors and indicators are driven | `hardware_interface.hpp/.cpp` | existing |
| 1.1.2 | SimulatedCabinet | Stand-in physical cabinet (4 drawers) with fault injection for the demo | the stand-in physics (NAT-1..4) and how physical state is persisted | `simulated_cabinet.hpp/.cpp` | new |
| 1.1.3 | Storage | Saves and loads named tables of text rows | persistence medium and file format (atomic text snapshots, append-only records) | `storage.hpp/.cpp` | new |
| 1.1.4 | MonitorTransport (abstract) + FakeMonitor | Delivers a batch of encoded messages / stands in for Part B | how bytes reach the Monitoring System (to agree with Part B) | `monitor_transport.hpp`, `fake_monitor.hpp/.cpp` | new |
| 1.1.5 | Clock | Monotonic time for timers, ISO-8601 UTC timestamps | time source (real clock vs manual test clock) | `clock.hpp/.cpp` | new |
| **1.2** | **Behaviour-hiding** | | | | |
| 1.2.1 | AccessControlManager | Who is signed in, what their role allows, when the session ends | credential check method, role->permission table, session rules | `access_control_manager.hpp/.cpp` | existing |
| 1.2.2 | DispensingController | Runs D1 SM2 for every dispense and replenish session | SM2 table, how sensor readings become "removed"/"closed", timers, session persistence | `dispensing_controller.hpp/.cpp` | existing |
| 1.2.3 | OperationalModeManager | Runs D1 SM1 and says what each mode permits | SM1 table, entry actions, mode->permitted-operation rules (C1) | `operational_mode_manager.hpp/.cpp` | existing |
| 1.2.4 | CommunicationManager | Runs D1 SM3: informs the Monitoring System, detects loss, buffers, resynchronizes | link policy: heartbeat, loss detection, outbox, cursor, resync | `communication_manager.hpp/.cpp` | existing |
| 1.2.5 | ConsoleUI | Local screen and keyboard | screen wording, layout and input handling (NFR-USB-*) | `console_ui.hpp/.cpp` | new |
| **1.3** | **Software-decision** | | | | |
| 1.3.1 | InventoryManager | Medication records: name, drawer, recorded quantity | record storage, medication->drawer map, persist-then-report rule | `inventory_manager.hpp/.cpp` | existing |
| 1.3.2 | EventLogger | The single, ordered, append-only audit trail | audit record format, ordering, persistence and retry | `event_logger.hpp/.cpp` | existing |
| 1.3.3 | FaultDetector | Raises, keeps and clears faults | fault rules (FLT-CHECK), thresholds, criticality, messages, clear conditions | `fault_detector.hpp/.cpp` | existing |
| 1.3.4 | MessageCodec | Messages <-> text | wire format of ADC<->Monitoring messages (flat JSON) | `message_codec.hpp/.cpp` | new |
| 1.3.5 | Config + shared types | Every tunable limit; the shared vocabulary of enums and records | the chosen limit values (D1 section 7: estimates); text spelling of each enum value | `config.hpp`, `types.hpp`, `types.cpp` | new |

All files are flat in `include/adc/` and `src/` (the CMake glob only picks up files directly
in those folders).

## Uses hierarchy (acyclic)

An arrow means "uses" (calls, or includes the header of). Each header includes only modules
below it.

```
main -> AdcSystem, Storage (--reset only), Clock, Config
AdcSystem -> ConsoleUI, AccessControlManager, DispensingController, OperationalModeManager,
             CommunicationManager, InventoryManager, FaultDetector, EventLogger,
             SimulatedCabinet, FakeMonitor, Storage, Clock, Config
DispensingController   -> AccessControlManager, InventoryManager, OperationalModeManager,
                          FaultDetector, HardwareInterface, EventLogger, Storage, Clock, Config
OperationalModeManager -> AccessControlManager, FaultDetector, EventLogger, HardwareInterface (indicator)
CommunicationManager   -> MonitorTransport, MessageCodec, EventLogger (read-only cursor), Storage, Clock, Config
FaultDetector          -> HardwareInterface, EventLogger, Clock, Config
InventoryManager       -> Storage, EventLogger, AccessControlManager (Replenish permission), Config
AccessControlManager   -> EventLogger, Clock, Config
ConsoleUI              -> types, HardwareInterface (signalFor() only, to word the indicator)
FakeMonitor            -> MonitorTransport, MessageCodec
SimulatedCabinet       -> HardwareInterface, Storage, Config
EventLogger            -> Storage, Clock
Config                 -> types
HardwareInterface, MonitorTransport, Storage, Clock, MessageCodec, types -> (nothing in the project)
```

Two additions to the brief's section 3 list, both downward and acyclic:

- **DispensingController -> Storage**: needed to save the active session, so a restart closes
  it as Interrupted and can never resume it (NFR-SAF-003).
- **ConsoleUI -> HardwareInterface::signalFor()**: a pure function that pairs each indicator
  level with its label, light pattern and buzzer, so the screen and the indicator use the
  same wording (NFR-USB-003).

### Upward notifications (listeners, registered by AdcSystem; a listener is not a "use")

| Source | Listener | Purpose |
|---|---|---|
| FaultDetector (new fault) | OperationalModeManager::onFaultDetected | SM1 faultDetected (FR-MODE-005) |
| FaultDetector (new fault) | DispensingController::onFaultDetected | SM2 faultDetected -> Interrupted (NFR-SAF-001) |
| FaultDetector (new fault) | CommunicationManager::publishAlarm | alarm message (FR-COM-002) |
| InventoryManager (change) | CommunicationManager::publishInventory | D1 Replenish activity "send inventory to monitor" (FR-COM-001) |
| OperationalModeManager (mode change) | CommunicationManager::publishStatus | status message (FR-COM-001) |
| OperationalModeManager (maintenance) | CommunicationManager::publishMaintenance | maintenance message (FR-COM-002) |
| CommunicationManager (link change) | FaultDetector::reportCommunicationLoss / clearCommunicationLoss | FR-FAULT-003 |
| DispensingController (session outcome) | AdcSystem -> ConsoleUI | plain outcome message (NFR-USB-001) |

**EventLogger never calls CommunicationManager.** CommunicationManager reads unsent events
with a cursor (`EventLogger::after(lastAckedId)`), so there is one source of truth for events
(NFR-REL-002).

## Control loop (AdcSystem, every `tickPeriod` = 200 ms, under one mutex)

`dispensing.tick()` -> `faults.check(drawersUnderControl, expectedCounts)` ->
`access.tick(busy)` -> `comm.tick()` -> `modes.refreshIndicator()`.

## Start-up sequence (AdcSystem)

1. log `SYSTEM_STARTUP`
2. `dispensing.recoverInterruptedSession()` (NFR-SAF-003)
3. command every lock LOCKED
4. `faults.selfTest(expectedCounts)` (NFR-REL-004)
5. `modes.completeStartup(selfTestFaults)`: SM1 Startup -> Normal or Fault
6. publish the full inventory to the Monitoring System
