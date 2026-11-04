# ADC ↔ Monitoring System interface (to agree with Part B)

D1 treats the Monitoring System as an external system (D1 sections 1.4 and 4). This file is
where Part A and Part B agree what crosses that boundary. Both sub-teams should approve any
change here.

## What the ADC must send (D1 requirements)

| D1 requirement | What is sent | When |
|---|---|---|
| FR-COM-001 | Operational status, inventory, system health | TBD |
| FR-COM-002 | Alarms (detected faults) and maintenance information | TBD |
| FR-COM-003 | Locally recorded event information | While communication is available |
| FR-COM-005 | Retained information, then synchronization | After communication is restored |

## What the ADC requires from the Monitoring System

TBD (for example acknowledgements, and configuration updates: D1 use case
"Receive Configuration Updates").

## Behaviour when the Monitoring System is unavailable (D1)

- Loss is detected within 5 seconds (NFR-PRF-003, FR-FAULT-003).
- The ADC keeps operating locally (FR-COM-004, NFR-AVL-001, NFR-AVL-004).
- Information that cannot be sent is stored locally (NFR-AVL-002, FR-LOG-004) and confirmed
  event records are never removed because of the outage (NFR-REL-002).
- Stored information is sent once the link is restored (NFR-AVL-003, FR-COM-005).

## Open questions for Part B

- Transport (TCP, HTTP, MQTT, ...) and message format
- Message fields and IDs; how duplicates are detected
- Heartbeat period and how each side detects the other is gone
- Synchronization handshake after reconnection
