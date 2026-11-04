# Traceability (D1 → D2)

The first five columns are the D1 traceability table (D1 section 6). For D2, fill in the last
two columns for each row: the precise specification item and the code (file / class::method)
that implements it.

Two corrections to D1 are applied here: NFR-SEC-001 traces to US-SEC-01 **AC-01** (D1 said
AC-03; credential privacy is AC-01), and NFR-PRF-002 cites **US-INV-01** (D1 said "IS-INV-01").

## Functional requirements

| Requirement | Requirement (short) | D1 story (AC) | D1 component | Stakeholder | D2 precise spec | Code |
|---|---|---|---|---|---|---|
| FR-AUTH-001 | Authenticate user before access to protected medication storage | US-AUTH-01 (AC-01, AC-02, AC-03) | Access Control Manager | Nurse, Pharmacist, Technician | | |
| FR-AUTH-002 | Determine user's authorization level after authentication | US-AUTH-01 (AC-02) | Access Control Manager | Nurse, Pharmacist, Technician | | |
| FR-AUTH-003 | Deny protected actions when permission is missing | US-SEC-01 (AC-02, AC-03) | Access Control Manager | Nurse, Pharmacist, Technician | | |
| FR-AUTH-004 | Record each successful/unsuccessful authentication attempt | US-LOG-01 (AC-02) | Access Control Manager; Event Logger | Nurse, Pharmacist, Technician | | |
| FR-INV-001 | Maintain quantity of each stored medication | US-INV-01 (AC-01) | Inventory Manager | Pharmacist, Technician | | |
| FR-INV-002 | Update quantity when medication is dispensed | US-INV-01 (AC-02) | Inventory Manager | Pharmacist, Technician | | |
| FR-INV-003 | Update quantity when authorized staff replenish | US-REL-01 (AC-05) | Inventory Manager; Access Control Manager | Pharmacist, Technician | | |
| FR-INV-004 | Record inventory changes in event log | US-LOG-01 (AC-01) | Inventory Manager; Event Logger | Pharmacist, Technician | | |
| FR-DISP-001 | Accept dispensing requests from an authenticated user | US-DISP-01 (AC-01) | Dispensing Controller; Access Control Manager | Nurse | | |
| FR-DISP-002 | Identify storage location of the requested medication | US-DISP-01 (AC-01) | Dispensing Controller; Inventory Manager | Nurse | | |
| FR-DISP-003 | Provide access to the storage location when authorized | US-DISP-01 (AC-01) | Dispensing Controller | Nurse | | |
| FR-DISP-004 | Detect whether the medication was removed after an authorized dispense | US-INV-01 (AC-02) | Dispensing Controller | Nurse | | |
| FR-DISP-005 | Check operation mode before a dispensing operation is completed | US-MODE-01 (AC-01, AC-02) | Dispensing Controller; Operational Mode Manager | Nurse, Technician | | |
| FR-DISP-006 | Prevent an additional dispense from the same request after removal | US-SAF-01 (AC-03) | Dispensing Controller | Nurse | | |
| FR-DISP-007 | Identify an unsuccessful dispense when medication is not removed | US-SAF-01 (AC-03) | Dispensing Controller | Nurse | | |
| FR-LOG-001 | Record the outcome of each dispensing operation | US-LOG-01 (AC-01, AC-02); US-AUD-01 (AC-01, AC-02) | Event Logger; Dispensing Controller | Nurse | | |
| FR-LOG-002 | Record detected system faults | US-LOG-01 (AC-03) | Event Logger; Fault Detector | Pharmacist, Nurse | | |
| FR-LOG-003 | Record changes to the operational mode | US-AUD-01 (AC-03) | Event Logger; Operational Mode Manager | Pharmacist, Nurse | | |
| FR-LOG-004 | Retain event log entries locally when Monitoring System is unavailable | US-REL-01 (AC-02); US-AVL-01 (AC-02) | Event Logger; Communication Manager | Pharmacist, Nurse | | |
| FR-FAULT-001 | Prevent affected medication operations when a fault could compromise safe dispensing | US-DISP-01 (AC-03); US-SAF-01 (AC-01) | Fault Detector; Dispensing Controller | Nurse | | |
| FR-FAULT-002 | Identify and indicate the type of detected fault | US-FAULT-01 (AC-02, AC-03); US-USB-01 (AC-02) | Fault Detector | Technician, Nurse | | |
| FR-FAULT-003 | Detect when communication with the Monitoring System is unavailable | US-FAULT-01 (AC-03); US-PRF-01 (AC-04) | Fault Detector; Communication Manager | Technician, Nurse | | |
| FR-FAULT-004 | Detect when a dispensing operation does not complete as expected | US-SAF-01 (AC-03) | Fault Detector; Dispensing Controller | Nurse | | |
| FR-MODE-001 | Support a Normal operational mode | US-MODE-01 (AC-01) | Operational Mode Manager | Technician | | |
| FR-MODE-002 | Support a Fault operational mode | US-MODE-01 (AC-02) | Operational Mode Manager | Technician | | |
| FR-MODE-003 | Support a Maintenance operational mode | US-MODE-01 (AC-03) | Operational Mode Manager | Technician | | |
| FR-MODE-004 | Restrict maintenance-mode operations by user authorization level | US-MODE-01 (AC-03) | Operational Mode Manager; Access Control Manager | Technician | | |
| FR-MODE-005 | Transition Normal → Fault on a fault and restrict dispensing in Fault mode | US-MODE-01 (AC-02); US-SAF-01 (AC-02) | Operational Mode Manager; Fault Detector; Dispensing Controller | Technician, Nurse | | |
| FR-COM-001 | Transmit status, inventory and system health to the Monitoring System | US-COM-01 (AC-01) | Communication Manager | Monitoring System | | |
| FR-COM-002 | Transmit alarms and maintenance information to the Monitoring System | US-COM-01 (AC-01) | Communication Manager; Fault Detector | Monitoring System | | |
| FR-COM-003 | Transmit locally recorded events when communication is available | US-COM-01 (AC-01, AC-03) | Communication Manager; Event Logger | Monitoring System | | |
| FR-COM-004 | Continue local operation when Monitoring System is unavailable | US-COM-01 (AC-02); US-AVL-01 (AC-01) | Communication Manager | Nurse | | |
| FR-COM-005 | Transmit retained information and synchronize after communication is restored | US-COM-01 (AC-03); US-AVL-01 (AC-04) | Communication Manager; Event Logger | Nurse; Monitoring System | | |

## Non-functional requirements

| Requirement | Requirement (short) | D1 story (AC) | D1 component | Stakeholder | D2 precise spec | Code |
|---|---|---|---|---|---|---|
| NFR-SAF-001 | Prevent dispensing when a safety-related fault is detected | US-SAF-01 (AC-01); US-DISP-01 (AC-03) | Fault Detector; Dispensing Controller; Operational Mode Manager | Nurse; Technician | | |
| NFR-SAF-002 | Disable dispensing while in Fault mode until cleared by an authorized user | US-SAF-01 (AC-02); US-MODE-01 (AC-02) | Operational Mode Manager; Dispensing Controller | Nurse; Technician | | |
| NFR-SAF-003 | Prevent a duplicate dose after interruption | US-SAF-01 (AC-03) | Dispensing Controller; Access Control Manager | Nurse | | |
| NFR-SEC-001 | Prevent users from viewing another user's credentials | US-SEC-01 (AC-01) | Access Control Manager | Nurse; Pharmacist; Technician | | |
| NFR-SEC-002 | Restrict medication and record access by user role | US-SEC-01 (AC-02, AC-03); US-AUTH-01 (AC-02); US-MODE-01 (AC-03) | Access Control Manager; Operational Mode Manager | Nurse; Pharmacist; Technician | | |
| NFR-SEC-003 | End inactive authenticated sessions after a timeout | US-SEC-01 (AC-04) | Access Control Manager | Nurse; Pharmacist; Technician | | |
| NFR-REL-001 | Preserve inventory and event records after restart | US-REL-01 (AC-01) | Inventory Manager; Event Logger | Pharmacist, Technician | | |
| NFR-REL-002 | Preserve event records during communication failure | US-REL-01 (AC-02); US-AVL-01 (AC-02) | Event Logger; Communication Manager | Pharmacist; Monitoring System; Technician | | |
| NFR-REL-003 | Maintain accurate inventory after dispensing or replenishment | US-REL-01 (AC-05); US-INV-01 (AC-02) | Inventory Manager; Dispensing Controller | Pharmacist; Technician | | |
| NFR-REL-004 | Verify hardware state before dispensing after restart | US-REL-01 (AC-03, AC-04) | Fault Detector; Operational Mode Manager; Dispensing Controller | Technician | | |
| NFR-AVL-001 | Continue safe local operation during communication loss | US-AVL-01 (AC-01); US-COM-01 (AC-02); US-FAULT-01 (AC-03) | Communication Manager; Operational Mode Manager | Nurse; Technician; Monitoring System | | |
| NFR-AVL-002 | Store unsent information while the Monitoring System is unavailable | US-AVL-01 (AC-02); US-REL-01 (AC-02) | Communication Manager; Event Logger | Pharmacist; Monitoring System | | |
| NFR-AVL-003 | Transmit stored information after communication is restored | US-AVL-01 (AC-04); US-COM-01 (AC-03) | Communication Manager; Event Logger | Pharmacist; Monitoring System | | |
| NFR-AVL-004 | Maintain safe local cabinet functions during communication loss | US-AVL-01 (AC-01, AC-03); US-COM-01 (AC-02); US-FAULT-01 (AC-03) | Communication Manager; Operational Mode Manager; Dispensing Controller | Nurse; Technician; Monitoring System | | |
| NFR-PRF-001 | Respond to authentication attempts within 5 seconds | US-PRF-01 (AC-01, AC-03); US-AUTH-01 (AC-02, AC-03) | Access Control Manager | Nurse; Pharmacist; Technician | | |
| NFR-PRF-002 | Update inventory within 5 seconds of confirmed retrieval | US-PRF-01 (AC-02); US-INV-01 (AC-02) | Inventory Manager; Dispensing Controller | Nurse; Pharmacist | | |
| NFR-PRF-003 | Detect Monitoring System communication loss within 5 seconds | US-PRF-01 (AC-04); US-FAULT-01 (AC-03) | Communication Manager; Fault Detector | Technician; Monitoring System | | |
| NFR-AUD-001 | Log every medication dispensing attempt | US-AUD-01 (AC-01); US-LOG-01 (AC-01, AC-02) | Event Logger; Dispensing Controller | Nurse; Pharmacist | | |
| NFR-AUD-002 | Record required details for each dispensing attempt | US-AUD-01 (AC-02) | Event Logger; Dispensing Controller | Nurse; Pharmacist | | |
| NFR-AUD-003 | Maintain event records in chronological order | US-AUD-01 (AC-05) | Event Logger | Pharmacist; Technician | | |
| NFR-AUD-004 | Log safety-related system events | US-AUD-01 (AC-03); US-LOG-01 (AC-02, AC-03) | Event Logger; Access Control Manager; Fault Detector; Operational Mode Manager | Nurse; Pharmacist; Technician | | |
| NFR-AUD-005 | Prevent unauthorized modification of audit records | US-AUD-01 (AC-04); US-SEC-01 (AC-02, AC-03) | Event Logger; Access Control Manager | Pharmacist; Technician | | |
| NFR-USB-001 | Provide clear success or failure messages | US-USB-01 (AC-01); US-DISP-01 (AC-01, AC-02, AC-03) | Access Control Manager; Dispensing Controller | Nurse; Pharmacist; Technician | | |
| NFR-USB-002 | Display clear errors without exposing confidential information | US-USB-01 (AC-02); US-FAULT-01 (AC-02, AC-03) | Fault Detector; Access Control Manager | Nurse; Pharmacist; Technician | | |
| NFR-USB-003 | Communicate critical warnings using more than colour alone | US-USB-01 (AC-03) | Fault Detector; Operational Mode Manager | Nurse; Pharmacist; Technician | | |
| NFR-USB-004 | Use clear and understandable interface language | US-USB-01 (AC-04) | Access Control Manager; Dispensing Controller; Operational Mode Manager | Nurse; Pharmacist; Technician | | |
