# Precise specification (Part A ADC, Deliverable 2)

This document is the D2 precise specification behind the code. Names match D1 (components,
state machines, requirement IDs) and the headers in `include/adc/`. Every function table row
and state-machine cell names the test that checks it (file `tests/test_*.cpp`, test name).

Contents: 1 4-variable model - 2 function tables - 3 state machines - 4 contracts of the
safety-critical operations - 5 invariants - 6 D1 clarifications C1..C10.

---

## 1. Four-variable model (Week 5)

`d` ranges over the drawers `D1..D4` (`AdcConfig::drawers`).

### 1.1 Monitored variables (environment quantities the ADC must know)

| Monitored | Type | Meaning |
|---|---|---|
| `m_drawerOpen(d)` | bool | drawer d is physically open |
| `m_lockEngaged(d)` | bool | the lock bolt of d is engaged |
| `m_itemCount(d)` | int >= 0 | units physically in d |
| `m_monitorReachable` | bool | the Monitoring System answers (SM3) |
| `m_keyboard` | text | what the user types (badge, PIN, choices) |

### 1.2 Controlled variables (environment quantities the ADC changes)

| Controlled | Type | Meaning |
|---|---|---|
| `c_lock(d)` | LOCKED / UNLOCKED | command to the lock actuator of d |
| `c_light` | steady green / slow flashing amber / fast flashing red | indicator light pattern |
| `c_buzzer` | on / off | audible alarm |
| `c_display` | text | indicator text and console output |
| `c_message` | message stream | messages to the Monitoring System |

### 1.3 Input and output variables (what the software sees) and their mappings

| Software variable | Direction | Mapping (IN / OUT) | Interface |
|---|---|---|---|
| `i_drawerOpen(d)` | input | IN: `= m_drawerOpen(d)`, or *no value* when the sensor has failed | `HardwareInterface::i_drawerOpen` |
| `i_lockEngaged(d)` | input | IN: `= m_lockEngaged(d)`, or *no value* | `HardwareInterface::i_lockEngaged` |
| `i_itemCount(d)` | input | IN: `= m_itemCount(d)`, or *no value* | `HardwareInterface::i_itemCount` |
| `i_transport` | input | IN: `delivered` iff `m_monitorReachable` | `MonitorTransport::send` result |
| `o_setLock(d, cmd)` | output | OUT: `c_lock(d) := cmd` | `HardwareInterface::o_setLock` |
| `o_setIndicator(level, text)` | output | OUT: `c_light, c_buzzer := signalFor(level)`; `c_display := text` | `HardwareInterface::o_setIndicator` |
| `o_send(batch)` | output | OUT: `c_message := batch` | `MonitorTransport::send` |

`signalFor(level)` (NFR-USB-003, never colour alone):

| level | label | `c_light` | `c_buzzer` |
|---|---|---|---|
| Ok | OK | steady green | off |
| Warning | WARNING | slow flashing amber | off |
| Fault | FAULT | fast flashing red | on |

### 1.4 NAT constraints (physics of the cabinet; modelled by SimulatedCabinet)

| ID | Constraint |
|---|---|
| NAT-1 | `m_drawerOpen(d)` can become true only while `!m_lockEngaged(d)`, except by force (tamper) |
| NAT-2 | `m_itemCount(d)` changes only while `m_drawerOpen(d)` |
| NAT-3 | `m_itemCount(d) >= 0` |
| NAT-4 | `m_lockEngaged(d)` becomes true only when `c_lock(d) = LOCKED`, `!m_drawerOpen(d)` and the lock is not jammed |

Tests: `CabinetTest.NAT_1_*`, `NAT_2_*`, `NAT_3_*`, `NAT_4_*` (test_simulated_cabinet.cpp).

---

## 2. Function tables (first true row wins; rows are disjoint and complete)

### AUTH-LOGIN — `AccessControlManager::authenticate(badge, pin)` (FR-AUTH-001/002/004, NFR-SEC-001)

Before evaluating: any open session ends (`SESSION_ENDED reason=new sign-in`).

| Row | Condition | Result | Log | Test |
|---|---|---|---|---|
| R1 | badge not a known user | AuthFailed, "Sign-in failed: badge or PIN not recognised." | AUTH_FAILURE (badge only) | `AccessTest.AuthLogin_R1_UnknownBadge_Fails` |
| R2 | known badge, `hash(salt:pin) != stored hash` | AuthFailed, same message as R1 | AUTH_FAILURE (badge only) | `AccessTest.AuthLogin_R2_WrongPin_Fails`, `AuthLogin_R1_R2_SameMessage` |
| R3 | otherwise | Ok; session started, role set, timer restarted | AUTH_SUCCESS (badge, role) | `AccessTest.AuthLogin_R3_Valid_SessionStartedRoleSet` |

### SESSION-TIMEOUT — `AccessControlManager::tick(busy)` (NFR-SEC-003, C2)

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | not signed in | nothing | `AccessTest.Logout` |
| R2 | busy (a drawer session is active or a drawer is under control) | timer held (restarted) | `AccessTest.C2_TimerHeldWhileBusy` |
| R3 | `now - lastActivity >= sessionTimeout` | session ends, SESSION_ENDED reason=inactivity | `AccessTest.NFR_SEC_003_TimeoutAtExactly60sNot59` |
| R4 | otherwise | nothing | `AccessTest.NFR_SEC_003_ActivityRestartsTimer` |

### DSP-REQ — `DispensingController::requestDispense(med, qty)` (FR-DISP-001/002/003/005, NFR-SAF-001)

Precondition of the table: SM2 is Idle or terminal (cell dispenseRequest -> AuthorizationPending /
beginSession). If SM2 is AwaitingRetrieval or AwaitingClosure, the cell is "stay / rejectRequest":
R1 is evaluated, otherwise R2; the running session is unchanged. Every refusal writes exactly
one `DISPENSE_OUTCOME REJECTED` record (NFR-AUD-001).

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | not authenticated, or role lacks Dispense | Rejected; ACCESS_DENIED logged | `DispenseTest.DspReq_R1_NotAuthenticated_Rejected`, `DspReq_R1_LacksDispensePermission_Rejected` |
| R2 | a session is active, or the previous session's drawer is still under control | Rejected ("Another drawer access is in progress...") | `DspReq_R2_SessionActive_Rejected`, `DspReq_R2_PreviousDrawerStillOpen_Rejected` |
| R3 | `!inventory.hasMedication(med)` | Rejected | `DspReq_R3_UnknownMedication_Rejected` |
| R4 | `qty < 1` | Rejected | `DspReq_R4_QuantityBelowOne_Rejected` |
| R5 | `!modes.dispensingAllowed()` | Rejected ("Dispensing is not available in <MODE> mode.") | `DspReq_R5_FaultMode_Rejected`, `DspReq_R5_MaintenanceMode_Rejected` |
| R6 | `inventory.quantityOf(med) < qty` | Rejected (US-DISP-01 AC-02) | `DspReq_R6_InsufficientStock_Rejected` |
| R7 | drawer `locationOf(med)` not closed, not locked, or a sensor gives no reading | Rejected ("Drawer Dx is not ready. Call a technician.") | `DspReq_R7_DrawerNotReady_Rejected` |
| R8 | otherwise | AwaitingRetrieval: `o_setLock(locationOf(med), UNLOCKED)`, baseline count stored, retrieval timer started | `DspReq_R8_Valid_AwaitingRetrievalOnlyThatDrawerUnlocked` |

### REP-REQ — `DispensingController::requestReplenish(med)` (FR-INV-003, C1)

Same as DSP-REQ with: R1 permission Replenish (`InventoryManager::replenishAuthorized`); R4 and
R6 do not apply; R5 uses `modes.replenishmentAllowed()`. The outcome record is
`REPLENISH_OUTCOME`. Units added are measured by the count sensor.
Tests: `DispenseTest.RepReq_R1_NurseDenied`, `RepReq_R5_FaultModeRefused_MaintenanceAllowed_C1`,
`Replenish_CountsAddedUnits`.

### DSP-SENSE — `DispensingController::tick()`, drawer under control (FR-DISP-004, FR-INV-002, NFR-PRF-002)

`delta = i_itemCount(d) - lastRecordedCount`, evaluated every tick (200 ms):

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | count has no value | nothing (FaultDetector raises SensorFailure) | `DispenseTest.FaultMidSession_InterruptedLockCommandedLaterRemovalsRecorded` |
| R2 | `delta < 0` | `recordRemoval(med, -delta)`; if a dispense is active: removed += -delta, fire SM2 medicationRemoved | `HappyPath_InventoryUpdatedBeforeClosingWithinOneTick` |
| R3 | `delta > 0` | `recordAddition(med, delta)`; replenish active: added += delta, fire medicationRemoved (stock changed); dispense active: removed -= delta (units put back) | `Replenish_CountsAddedUnits` |
| R4 | the inventory update fails (R2/R3) | report InventoryUpdateFailed; `lastRecordedCount` unchanged, so the change is retried next tick | `InventoryStorageFailure_InventoryUpdateFailedFault` |
| R5 | `delta = 0` | nothing | `SM2_AwaitingRetrieval_CompartmentClosed_Ignore` |

Then: an open -> closed edge of `i_drawerOpen(d)` fires SM2 compartmentClosed; a finished
session's drawer is released once it reads closed.

### DSP-CLOSE — SM2 AwaitingClosure + compartmentClosed (FR-DISP-006, FR-FAULT-004)

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | replenish session, or `removed == requested` | Completed: lockCompartment, logDispense | `SM2_AwaitingClosure_CompartmentClosed_Completed` |
| R2 | dispense with `removed != requested` | not Completed: report IncompleteDispense (requested vs removed) -> SM2 faultDetected -> Interrupted; SM1 -> Fault | `SM2_AwaitingClosure_CompartmentClosed_WrongQuantity_IncompleteDispenseInterrupted` |

### DSP-TIMEOUT — SM2 timers (FR-DISP-007, C5)

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | AwaitingRetrieval and `now >= start + retrievalTimeout` (30 s) | TimedOut; dispense: IncompleteDispense (C5) -> Fault mode; replenish: no fault | `SM2_AwaitingRetrieval_RetrievalTimeout_TimedOut`, `C5_RetrievalTimeout_IncompleteDispense_FaultMode` |
| R2 | AwaitingClosure and `now >= firstRemoval + closureTimeout` (20 s) | TimedOut; dispense: IncompleteDispense (C5); replenish: StorageNotClosed | `SM2_AwaitingClosure_ClosureTimeout_TimedOut_C5` |
| R3 | otherwise | nothing | `SM2_AwaitingRetrieval_MedicationRemoved_AwaitingClosure` |

### FLT-CHECK — `FaultDetector::check(authorized, expected)` (FR-FAULT-001/002, NFR-SAF-001)

Per drawer d (first true row wins), then R6 once:

| Row | Condition | Fault raised | Test |
|---|---|---|---|
| R1 | any of `i_drawerOpen(d)`, `i_lockEngaged(d)`, `i_itemCount(d)` has no value | SensorFailure(d) | `FaultTest.FltCheck_R1_NoSensorReading_SensorFailure`, `FltCheck_R1_AppliesEvenToAuthorizedDrawer` |
| R2 | open and `d` not authorized | UnauthorizedAccess(d) | `FltCheck_R2_OpenWithoutAuthorization_UnauthorizedAccess`, `FltCheck_R2_OpenWithAuthorization_NoFault` |
| R3 | closed, lock not engaged, not authorized, continuously for >= lockConfirmTimeout (2 s) | StorageNotClosed(d) | `FltCheck_R3_UnlockedPastLockConfirmTimeout_StorageNotClosed`, `FltCheck_R3_RelockInTime_NoFault` |
| R4 | closed, locked, not authorized, count != recorded quantity | IncompleteDispense(d) (discrepancy) | `FltCheck_R4_IdleDrawerCountDiscrepancy_IncompleteDispense`, `FltCheck_R4_AuthorizedDrawerNotChecked` |
| R5 | otherwise | none | `FltCheck_R5_HealthyCabinet_NoFault` |
| R6 | `EventLogger::healthy() == false` | StorageFailure | `FltCheck_R6_LoggerUnhealthy_StorageFailure` |

Other fault sources: InventoryUpdateFailed (DSP-SENSE R4), IncompleteDispense (DSP-CLOSE R2,
DSP-TIMEOUT), StartupCheckFailed (`selfTest`), CommunicationLoss (SM3 listener). A fault
already active is not raised again (`ReportIsIdempotentWhileActive`).

**Criticality:** every fault type is critical except CommunicationLoss
(`INV_FLT_3_CommunicationLossNotCritical`).

### FLT-CLEAR — `FaultDetector::tryClear(actor, expected)` (NFR-SAF-002)

For each active critical fault, its condition is re-checked on the hardware:

| Fault | Condition gone when | Test |
|---|---|---|
| SensorFailure(d) | all three sensors of d read | `TryClear_SensorFailureAfterRepair` |
| UnauthorizedAccess(d), StorageNotClosed(d) | d reads closed and locked | `TryClear_DeniedWhileConditionPersists_SucceedsAfterRepair` |
| IncompleteDispense(d), StartupCheckFailed(d) | d closed, locked, sensors reading, count == record | `SM1_Fault_FaultCleared_AuthorizedAndGone_Normal`, Scenario 6/12 |
| InventoryUpdateFailed, StorageFailure | `EventLogger::retryPending()` succeeds | `TryClear_StorageFailureAfterStorageRecovers` |

Each removed fault logs FAULT_CLEARED, each remaining one FAULT_CLEAR_DENIED.

### MODE-DIAG — `OperationalModeManager::runDiagnostics(expected)` (use case "Run Diagnostic Check")

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | not authorised for RunDiagnostics | denied, ACCESS_DENIED | `ModeTest.ModeDiag_R1_NurseDenied` |
| R2 | mode not Maintenance or Fault | refused | `ModeDiag_R2_NormalModeRefused` |
| R3 | otherwise | `FaultDetector::diagnose`; MAINTENANCE_ACTIVITY with findings; maintenance message | `ModeDiag_R3_InMaintenance_RunsAndLogs` |

### CFG-UPDATE — inbound `config` message (clarification C8)

| Row | Condition | Result | Test |
|---|---|---|---|
| R1 | key == `heartbeatPeriodMs`, value an integer with `tickPeriod <= value < commLossTimeout` | heartbeat changed; CONFIG_UPDATED APPLIED | `CommTest.C8_ConfigUpdateAppliedAndLogged` |
| R2 | otherwise (unknown key, bad or out-of-range value) | nothing changes; CONFIG_UPDATED REJECTED | `C8_ConfigUnknownKeyOrBadValueRejectedAndLogged` |

### Role -> permission table (AccessControlManager, the module's secret)

| Permission | Nurse | Pharmacist | Technician | D1 source |
|---|---|---|---|---|
| Dispense | yes | yes | | use case diagram; US-DISP-01 |
| ViewInventory | yes | yes | | use case "View Inventory" |
| Replenish | | yes | | "Replenish Inventory"; US-REL-01 |
| ViewLog | | yes | yes | NFR-SEC-002 |
| EnterMaintenance / ExitMaintenance | | | yes | FR-MODE-003/004 |
| RunDiagnostics | | | yes | "Run Diagnostic Check" |
| ClearFault | | | yes | "Acknowledge/Clear Fault"; NFR-SAF-002 |
| ModifyAuditRecords | | | | NFR-AUD-005: nobody |

Test: `AccessTest.PermissionTable_EveryCell`, `NFR_AUD_005_NobodyMayModifyAuditRecords`.

---

## 3. State machines (transition tables, Week 7)

Cell kinds used in the code: **Go** (change state; the target's entry action runs), **Guarded**
(D1 guard; false = stay and the guard's own denial is logged), **Stay** (internal action),
**Ignore** (no change, no log), **Reject** (no change + `UNEXPECTED_EVENT` record + rejection
message, user-caused), **Illegal** (unreachable if the table is right: `failSafe()` disables
dispensing and logs). No cell is blank.

`Off` is the D1 final pseudo-state reached by powerOff (SM1, SM3); it is not a new D1 state.

### 3.1 SM1 Operational Modes — `src/operational_mode_manager.cpp` `kTable[mode][event]`

Entry actions (one place, `enter()`): Normal = enterNormalMode / resumeNormalOperation (indicator
OK); Maintenance = restrictOperations (indicator WARNING); Fault = disableDispensing +
displayFault (indicator FAULT + buzzer); Off = shutdown (SYSTEM_SHUTDOWN). Every change logs
MODE_CHANGED `FROM->TO reason=...` and notifies CommunicationManager (status). "logFault" is the
FAULT_DETECTED record written by FaultDetector (one per fault, C6).

| State \ Event | startupChecksPassed | startupCheckFailed | enterMaintenance | exitMaintenance | faultDetected | faultCleared | powerOff |
|---|---|---|---|---|---|---|---|
| **Startup** | Go Normal | Go Fault | Reject | Reject | Go Fault | Ignore | Go Off |
| **Normal** | Ignore | Ignore | Guarded [authorizedForMaintenance] Maintenance; else stay, ACCESS_DENIED | Reject | Go Fault | Ignore | Go Off |
| **Maintenance** | Ignore | Ignore | Ignore (D1) | Guarded [authorizedForMaintenance] Normal; else stay, ACCESS_DENIED | Go Fault | Ignore | Go Off |
| **Fault** | Ignore | Ignore | Reject | Reject | Stay (C6: fault still logged and alarmed) | Guarded [authorizedUser and all critical faults gone] Normal; else stay, log denial | Go Off |
| **Off** | Illegal | Illegal | Illegal | Illegal | Illegal | Illegal | Ignore |

Only critical faults fire faultDetected; CommunicationLoss changes the indicator to WARNING only.

Tests (test_mode_manager.cpp, one per cell group): `SM1_Startup_StartupChecksPassed_Normal`,
`SM1_Startup_StartupCheckFailed_Fault`, `SM1_Startup_EnterMaintenance_RejectedAndLogged`,
`SM1_Startup_ExitMaintenance_RejectedAndLogged`, `SM1_Startup_FaultDetected_Fault`,
`SM1_Startup_FaultCleared_Ignore`, `SM1_Startup_PowerOff_Off`,
`SM1_Normal_StartupChecksPassed_Ignore`, `SM1_Normal_StartupCheckFailed_Ignore`,
`SM1_Normal_EnterMaintenance_Authorized_Maintenance`,
`SM1_Normal_EnterMaintenance_NotAuthorized_StayRejectLogAttempt`,
`SM1_Normal_ExitMaintenance_RejectedAndLogged`, `SM1_Normal_FaultDetected_Fault`,
`SM1_Normal_FaultCleared_Ignore`, `SM1_Normal_PowerOff_Off`,
`SM1_Normal_CommunicationLoss_StaysNormalWithWarning`,
`SM1_Maintenance_EnterMaintenance_Ignore`, `SM1_Maintenance_ExitMaintenance_Authorized_Normal`,
`SM1_Maintenance_ExitMaintenance_NotAuthorized_RejectLog`, `SM1_Maintenance_FaultDetected_Fault`,
`SM1_Maintenance_PowerOff_Off`, `SM1_Maintenance_StartupChecksPassed_Ignore`,
`SM1_Fault_EnterMaintenance_RejectedAndLogged`, `SM1_Fault_ExitMaintenance_RejectedAndLogged`,
`SM1_Fault_FaultDetected_StayButStillLogged_C6`,
`SM1_Fault_FaultCleared_NotAuthorizedUser_StayLogDenial`,
`SM1_Fault_FaultCleared_ConditionPersists_StayLogDenial`,
`SM1_Fault_FaultCleared_AuthorizedAndGone_Normal`, `SM1_Fault_StartupChecksPassed_Ignore`,
`SM1_Fault_PowerOff_Off`, `SM1_Off_AnyEvent_Illegal_FailSafe`, `SM1_Off_PowerOff_Ignore`.

### 3.2 SM2 Dispensing Lifecycle — `src/dispensing_controller.cpp` `kTable[state][event]`

Entry actions (`enter()`): AwaitingRetrieval = unlockCompartment (UNLOCK, baseline, retrieval
timer, drawer under control, session saved); AwaitingClosure = closure timer, session saved;
every terminal state = `finish()`: lockCompartment if this session unlocked (INV-DSP-1),
exactly one outcome record (INV-DSP-2), saved session cleared, outcome listeners, then the C5
fault for TimedOut.

| State \ Event | dispenseRequest | authorizationApproved | authorizationDenied | medicationRemoved | cancelRequest | retrievalTimeout | compartmentClosed | closureTimeout | faultDetected |
|---|---|---|---|---|---|---|---|---|---|
| **Idle** | Go AuthorizationPending / beginSession | Ignore | Ignore | Reject / logUnexpectedEvent | Ignore | Ignore | Ignore | Ignore | Ignore |
| **AuthorizationPending** | Stay / rejectRequest | Guarded [authorizedUser] AwaitingRetrieval / unlockCompartment | Go Rejected / rejectRequest, logAttempt | Ignore | Go Cancelled / logCancellation | Ignore | Ignore | Ignore | Go Interrupted / disableDispensing, logFault |
| **AwaitingRetrieval** | Stay / rejectRequest | Ignore | Ignore | Go AwaitingClosure / updateInventory | Go Cancelled / lockCompartment, logCancellation | Go TimedOut / lockCompartment, logFailedDispense | Ignore (nothing removed) | Ignore | Go Interrupted / disableDispensing, lockCompartment, logFault |
| **AwaitingClosure** | Stay / rejectRequest | Ignore | Ignore | Stay / updateInventory | Reject ("close the drawer") | Ignore | Guarded DSP-CLOSE: R1 Completed / lockCompartment, logDispense; R2 raise IncompleteDispense | Go TimedOut / logFault (+ C10 lock) | Go Interrupted / disableDispensing, lockCompartment, logFault |
| **Rejected, Cancelled, TimedOut, Completed, Interrupted** | Go AuthorizationPending (new session only) | Ignore | Ignore | Reject / logUnexpectedEvent | Ignore | Ignore | Ignore | Ignore | Ignore |

AuthorizationPending is transient inside `requestDispense`, so its cells are checked by the
table query `DispensingController::transitionFor` (`DispenseTest.SM2_Table_MatchesD1`) as well
as behaviourally where reachable.

Tests (test_dispensing.cpp): `SM2_Table_MatchesD1`,
`SM2_Idle_DispenseRequest_AuthorizationPending_BeginsSession`,
`SM2_Idle_MedicationRemoved_LogUnexpectedEvent`, `SM2_Idle_CancelRequest_Ignore`,
`SM2_AuthorizationPending_AuthorizationApproved_AwaitingRetrieval`,
`SM2_AuthorizationPending_AuthorizationDenied_Rejected`,
`SM2_AwaitingRetrieval_DispenseRequest_StayRejectRequest`,
`SM2_AwaitingRetrieval_MedicationRemoved_AwaitingClosure`,
`SM2_AwaitingRetrieval_CancelRequest_Cancelled`, `SM2_AwaitingRetrieval_RetrievalTimeout_TimedOut`,
`SM2_AwaitingRetrieval_CompartmentClosed_Ignore`, `SM2_AwaitingRetrieval_FaultDetected_Interrupted`,
`SM2_AwaitingClosure_DispenseRequest_StayRejectRequest`,
`SM2_AwaitingClosure_MedicationRemoved_StayUpdateInventory`,
`SM2_AwaitingClosure_CancelRequest_RejectCloseTheDrawer`,
`SM2_AwaitingClosure_CompartmentClosed_Completed`,
`SM2_AwaitingClosure_CompartmentClosed_WrongQuantity_IncompleteDispenseInterrupted`,
`SM2_AwaitingClosure_ClosureTimeout_TimedOut_C5`, `SM2_AwaitingClosure_FaultDetected_Interrupted`,
`SM2_Completed_DispenseRequest_NewSession`, `SM2_Terminal_MedicationRemoved_LogUnexpectedEvent`,
`SM2_Terminal_CancelRequest_Ignore`.

### 3.3 SM3 Monitoring System Communication — `src/communication_manager.cpp` `kTable[state][event]`

Events from observations: a successful exchange fires connectionEstablished (or
connectionRestored when Disconnected); a successful exchange with nothing left unsent fires
synchronizationComplete; no successful exchange for `commLossTimeout` (3 s, C3) fires
communicationLost; every publish fires outgoingInformation. Entry actions (`enter()`): every
state logs COMM_STATE_CHANGED and notifies the link listeners; Resynchronizing re-queues the
full status and inventory (beginResynchronization); Off saves the outbox.

| State \ Event | connectionEstablished | communicationLost | connectionRestored | synchronizationComplete | outgoingInformation | powerOff |
|---|---|---|---|---|---|---|
| **Connecting** | Go Connected / beginNormalTransmission | Go Disconnected / enableLocalBuffering, logCommunicationFault (C4) | Ignore | Ignore | Stay / queueInformation | Go Off |
| **Connected** | Ignore (D1) | Go Disconnected / enableLocalBuffering, logCommunicationFault | Ignore | Ignore | Stay / send now | Go Off |
| **Disconnected** | Ignore | Ignore | Go Resynchronizing / beginResynchronization | Ignore | Stay / queueInformation | Go Off |
| **Resynchronizing** | Ignore | Go Disconnected / retainStoredInformation | Ignore | Go Connected / resumeNormalTransmission | Stay / queueInformation | Go Off |
| **Off** | Illegal | Illegal | Illegal | Illegal | Illegal | Ignore |

SM3's failSafe only logs: CommunicationManager has no dispensing function to disable.

Tests (test_communication.cpp): `SM3_Table_MatchesD1`,
`SM3_Connecting_ConnectionEstablished_Connected`, `SM3_Connecting_CommunicationLost_Disconnected_C4`,
`SM3_Connecting_OutgoingInformation_Queued`, `SM3_Connecting_PowerOff_Off`,
`SM3_Connecting_ConnectionRestored_Ignore`, `SM3_Connected_ConnectionEstablished_Ignore`,
`SM3_Connected_CommunicationLost_Disconnected`, `SM3_Connected_OutgoingInformation_SentNow`,
`SM3_Connected_PowerOff_Off`, `SM3_Disconnected_ConnectionRestored_Resynchronizing`,
`SM3_Disconnected_OutgoingInformation_Queued`, `SM3_Disconnected_CommunicationLost_Ignore`,
`SM3_Disconnected_PowerOff_Off`, `CommResyncTest.SM3_Resynchronizing_SynchronizationComplete_Connected`,
`SM3_Resynchronizing_CommunicationLost_Disconnected`, `SM3_Resynchronizing_OutgoingInformation_Queued`,
`SM3_Resynchronizing_PowerOff_Off`, `SM3_Off_OutgoingInformation_Illegal_Logged`, `SM3_Off_PowerOff_Ignore`.

---

## 4. Contracts of the safety-critical operations

### `DispensingController::requestDispense(med, qty)`
- **Pre:** none (all inputs are checked).
- **Post:** exactly one of: (a) DSP-REQ R8 held, `state() = AwaitingRetrieval`, exactly drawer
  `locationOf(med)` is commanded UNLOCKED and no other drawer was unlocked (INV-DSP-1); or
  (b) one `DISPENSE_OUTCOME REJECTED` record exists for this attempt, no lock command was sent,
  and a session that was already active is unchanged.

### `DispensingController::tick()` — closure
- **Pre:** state AwaitingClosure; `i_drawerOpen(d)` changed from true to false since the last tick.
- **Post:** inventory equals the last sensed count (DSP-SENSE first). If `removed == requested`:
  state Completed, `c_lock(d) = LOCKED`, one COMPLETED record. Otherwise: an IncompleteDispense
  fault is active, state Interrupted, mode Fault, one INTERRUPTED record. Either way the
  session is terminal and can never become active again (INV-DSP-3).

### `DispensingController::onFaultDetected(fault)`
- **Pre:** fault was just raised by FaultDetector.
- **Post:** if `fault.critical` and a session was active: state Interrupted, `c_lock(d) = LOCKED`
  commanded at once, the drawer stays under control until it reads closed (later removals are
  recorded, INV-DSP-4), one INTERRUPTED record. Non-critical faults (CommunicationLoss) change
  nothing (NFR-AVL-004).

### `OperationalModeManager::requestClearFault(expected)`
- **Pre:** none.
- **Post:** mode was Fault, the user holds ClearFault and every critical fault's condition is
  gone on the hardware => mode Normal, no critical fault active (INV-MODE-1), FAULT_CLEARED per
  fault, MODE_CHANGED. Otherwise the mode is unchanged and the denial is logged (ACCESS_DENIED
  or FAULT_CLEAR_DENIED). In any other mode: no change (Ignore).

### Start-up (`AdcSystem` constructor / restart)
- **Pre:** the data folder holds the state of the previous run (or nothing).
- **Post:** SYSTEM_STARTUP logged; a saved active session is Interrupted with one outcome record
  and is never resumed (NFR-SAF-003); every lock is commanded LOCKED; the self-test ran on every
  drawer; mode is Normal iff every drawer is closed, locked, reading and matches its record and
  no critical fault is active, Fault otherwise (NFR-REL-004); inventory and event log equal those
  saved before the restart (NFR-REL-001).

---

## 5. Invariants

| ID | Invariant | Module | Test |
|---|---|---|---|
| INV-CFG-1 | durations > 0; heartbeatPeriod < commLossTimeout; commLossTimeout + tickPeriod < 5 s | Config | `Config.INV_CFG_1_LossDetectedWithinFiveSeconds` |
| INV-CLK-1 | `now()` never decreases | Clock | `Clock.ManualClockAdvancesAndFormatsIso` |
| INV-STO-1 | a snapshot file holds the previous or the new complete snapshot, never a mix | Storage | `StorageTest.RealWriteFailureLeavesOldFileIntact` |
| INV-LOG-1 | record IDs are 1..n, no gaps, increasing | EventLogger | `EventLoggerTest.INV_LOG_1_IdsAreOneToNWithoutGaps` |
| INV-LOG-2 | every accepted record is kept; the unsaved ones are the last `unsavedCount()` | EventLogger | `NFR_REL_002_StorageFailureUnhealthyNothingLostRetried` |
| INV-LOG-3 | no operation edits, deletes or reorders a record | EventLogger | `NFR_AUD_005_ReturnedCopiesCannotChangeTheLog` |
| INV-ACC-1 | signed in <=> currentUser != "" <=> role has a value | AccessControlManager | `AccessTest.Logout` |
| INV-ACC-2 | no PIN, salt or hash in any state, return value, record or message | AccessControlManager | `NFR_SEC_001_NoPinInAnyRecordOrMessage`, `ScenarioTest.NoPinAnywhere` |
| INV-ACC-3 | a session never outlives sessionTimeout of inactivity unless busy | AccessControlManager | `NFR_SEC_003_TimeoutAtExactly60sNot59` |
| INV-INV-1 | every recorded quantity >= 0 | InventoryManager | `InventoryTest.INV_INV_1_NeverNegative` |
| INV-INV-2 | memory equals the last saved snapshot (failed save rolled back) | InventoryManager | `StorageFailureRollsBackAndReturnsStorageError` |
| INV-INV-3 | each drawer holds at most one medication | InventoryManager | `FR_INV_001_SeededLibraryAndLookups` |
| NAT-1..4 | cabinet physics (section 1.4) | SimulatedCabinet | `CabinetTest.NAT_*` |
| INV-FLT-1 | at most one active fault per (type, drawer) | FaultDetector | `ReportIsIdempotentWhileActive` |
| INV-FLT-2 | one FAULT_DETECTED per fault that became active | FaultDetector | `FR_LOG_002_FaultLogged` |
| INV-FLT-3 | CommunicationLoss is never critical; all other types are | FaultDetector | `INV_FLT_3_CommunicationLossNotCritical` |
| INV-MODE-1 | mode == Normal => no critical fault (NFR-SAF-002) | OperationalModeManager | `ModeTest.INV_MODE_1_HoldsThroughAWholeCycle` |
| INV-MODE-2 | dispensingAllowed <=> Normal, no critical fault, fail-safe not tripped (C1) | OperationalModeManager | same + `DspReq_R5_*` |
| INV-MODE-3 | one MODE_CHANGED per mode change | OperationalModeManager | `FR_LOG_003_EveryModeChangeLogged` |
| INV-DSP-1 | at most one drawer commanded UNLOCKED, only for the active session | DispensingController | `DspReq_R8_Valid_AwaitingRetrievalOnlyThatDrawerUnlocked`, `SM2_AwaitingClosure_ClosureTimeout_TimedOut_C5` |
| INV-DSP-2 | every attempt produces exactly one outcome record | DispensingController | `INV_DSP_2_EveryAttemptOneOutcome` |
| INV-DSP-3 | a terminal session never re-enters an active state | DispensingController | `SM2_Table_MatchesD1`, `NFR_SAF_003_SecondDispenseFromSameSessionImpossible` |
| INV-DSP-4 | each sensed count change of a drawer under control is recorded within one tick | DispensingController | `HappyPath_InventoryUpdatedBeforeClosingWithinOneTick` |
| INV-COM-1 | the event cursor only advances over a contiguous acknowledged run | CommunicationManager | `ResyncDeliversEveryEventOnceInOrder` |
| INV-COM-2 | no event record is removed because of the link | CommunicationManager | `BufferingGrowsNothingRemoved` |
| INV-COM-3 | alarms/maintenance leave the outbox only when acknowledged; one status and one inventory per medication queued | CommunicationManager | `AlarmsNeverDropped`, `LatestWinsStatusAndInventory` |
| INV-COM-4 | outbox and cursor in storage equal memory after every change | CommunicationManager | `OutboxAndCursorSurviveRestart` |
| INV-COM-5 | nothing blocks local operation; `requiresMonitoringSystem()` is false (C7) | CommunicationManager | `LocalDispensingWorksWhileOffline`, `C7_NoFunctionRequiresMonitoringSystem` |
| INV-FAKE-1 | the stand-in stores each message ID once, in order | FakeMonitor | `ResyncDeliversEveryEventOnceInOrder` |
| INV-UI-1 | no ANSI escape character in any output | ConsoleUI | `UiTest.INV_UI_1_NoAnsiEscapeAnywhere` |
| INV-UI-2 | a PIN is never written to the output | ConsoleUI | `UiTest.INV_UI_2_PinNotEchoed` |
| INV-SYS-1 | modules are touched only under AdcSystem's mutex | AdcSystem | by construction (one lock); scenarios |
| INV-SYS-2 | control-loop order dispensing -> faults -> access -> comm -> indicator | AdcSystem | scenarios 1-12 |

---

## 6. D1 clarifications applied

| ID | D1 issue | Resolution implemented | Where |
|---|---|---|---|
| C1 | FR-DISP-005 "check mode" does not say which modes allow dispensing | Dispensing only in Normal with no critical fault; Maintenance and Fault refuse it. Replenishment in Normal and Maintenance, refused in Fault. | `OperationalModeManager::dispensingAllowed/replenishmentAllowed`; DSP-REQ R5 |
| C2 | NFR-SEC-003 timeout is "team-defined" | 60 s (`AdcConfig::sessionTimeout`, `--session-timeout`), held while a dispense/replenish session is active or its drawer is under control | `AccessControlManager::tick(busy)` |
| C3 | SM3 guard "noResponseFor5Seconds" vs NFR-PRF-003 "within 5 s" | Loss after 3 s without a successful exchange (`commLossTimeout`), retry/heartbeat every 1 s: detection < 5 s (measured live: 2.5 s) | `CommunicationManager::tick` |
| C4 | SM3 has no exit from Connecting if the Monitoring System is never reachable | Connecting + communicationLost -> Disconnected | SM3 table |
| C5 | D1 Activity 2: a dispense not removed/closed leads to Fault mode; SM2 only ends in TimedOut | Both: SM2 TimedOut and FaultDetector IncompleteDispense (FR-FAULT-004) -> SM1 Fault | `DispensingController::finish` |
| C6 | SM1 "Fault: faultDetected / ignore" vs FR-LOG-002 "log every fault" | Mode unchanged, the new fault is still logged and alarmed | SM1 Fault/faultDetected = Stay |
| C7 | D1 Activity 3 "needs monitor? -> refuse" | No ADC function needs the Monitoring System; `requiresMonitoringSystem()` returns false | `CommunicationManager` |
| C8 | Use case "Receive Configuration Updates" has no requirement ID | Only `heartbeatPeriodMs` is accepted (range tickPeriod..commLossTimeout); every update logged CONFIG_UPDATED APPLIED/REJECTED | CFG-UPDATE |
| C9 | D1 traceability typos | Kept as fixed in `docs/traceability.md` | — |
| **C10 (new)** | SM2 AwaitingClosure + closureTimeout lists only "logFault", so the open drawer would never be commanded to lock | Every terminal state's entry action commands LOCKED for a drawer this session unlocked (INV-DSP-1: unlocked only for an active session); the lock engages once the drawer is closed | `DispensingController::finish`; test `SM2_AwaitingClosure_ClosureTimeout_TimedOut_C5` — **please confirm with the team** |
| **C11 (new)** | SM2 does not say what a replenish session's "medicationRemoved" is | For replenish, any sensed stock change is the medicationRemoved column (stock moved); a replenish retrieval timeout raises no fault, a replenish closure timeout raises StorageNotClosed | DSP-SENSE R3, DSP-TIMEOUT — **please confirm** |
