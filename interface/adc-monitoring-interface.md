# ADC ↔ Monitoring System interface (to agree with Part B)

> **Status: Proposed by Part A, pending Part B agreement.** Nothing here is final until both
> sub-teams approve it. Part A's code isolates every detail below in two modules
> (`MessageCodec` for the format, `MonitorTransport` for the transport), so a change agreed with
> Part B touches only those two files.

D1 treats the Monitoring System as an external system (D1 sections 1.4 and 4). This file is
where Part A and Part B agree what crosses that boundary. Both sub-teams should approve any
change here.

## What the ADC must send (D1 requirements)

| D1 requirement | What is sent | When |
|---|---|---|
| FR-COM-001 | Operational status, inventory, system health | `status` on every mode change and at start-up; `inventory` on every recorded change and at start-up; both again after reconnection |
| FR-COM-002 | Alarms (detected faults) and maintenance information | `alarm` when a fault is raised; `maintenance` on maintenance enter/exit/diagnostics |
| FR-COM-003 | Locally recorded event information | `event` for every audit record, in ID order, while communication is available |
| FR-COM-005 | Retained information, then synchronization | after the link returns: everything unsent, plus full status and inventory, then normal operation |

## Provides / Requires

**The ADC provides** (to the Monitoring System): status, inventory, alarms, maintenance
information and the full ordered event stream of one cabinet (`cabinet_id`).

**The ADC requires** (from the Monitoring System):
- an **acknowledgement** of every message ID it has stored (so the ADC can stop re-sending it);
- optionally, **configuration updates** (D1 use case "Receive Configuration Updates";
  clarification C8: only whitelisted keys are applied).

**The ADC does not require the Monitoring System for any local function** (clarification C7,
FR-COM-004): dispensing, replenishment, faults and maintenance all work while it is down.

**Protocol is a secret.** The transport (TCP, HTTP, MQTT, ...) lives behind
`MonitorTransport::send(batch) -> { delivered, ackedIds, inbound }`; the format lives in
`MessageCodec`. For the D2 demo the far end is `FakeMonitor`, a stand-in, not Part B's code.

## Message format (proposal)

One flat JSON object per message, UTF-8. Strings use JSON escaping (`\" \\ \n \r \t \b \f`,
`\u00XX` for other control characters). Numbers and booleans are bare. Every ADC -> Monitoring
message starts with the four common fields.

| Common field | Type | Meaning |
|---|---|---|
| `cabinet_id` | string | e.g. `"ADC-01"` (`AdcConfig::cabinetId`) |
| `type` | string | `status`, `inventory`, `alarm`, `maintenance`, `event`, `heartbeat` |
| `id` | string | unique per cabinet: `MSG-<n>` (outbox), `EVT-<event id>` (events), `HB-<n>` (heartbeats) |
| `timestamp` | string | ISO-8601 UTC with milliseconds, e.g. `"2026-11-04T05:12:53.376Z"` |

## Message table

| Direction | Message | Fields (besides the common ones) | Meaning | Timing |
|---|---|---|---|---|
| ADC → MS | `status` | `mode` (STARTUP/NORMAL/MAINTENANCE/FAULT/OFF), `critical_fault` (bool), `reason` (string) | operational status and system health (FR-COM-001) | on every mode change, at start-up, on resync; latest value wins while unsent |
| ADC → MS | `inventory` | `medication`, `name`, `drawer`, `quantity` (int) | recorded quantity of one medication (FR-COM-001) | on every recorded change, at start-up, on resync; latest value per medication wins while unsent |
| ADC → MS | `alarm` | `fault_type` (e.g. StorageNotClosed), `drawer`, `critical` (bool), `message` (plain text) | a detected fault (FR-COM-002) | when raised; never dropped |
| ADC → MS | `maintenance` | `activity` (text), `user` (badge) | maintenance enter/exit/diagnostics (FR-COM-002) | when it happens; never dropped |
| ADC → MS | `event` | `event_id` (int), `event_type` (e.g. DISPENSE_OUTCOME), `user`, `medication`, `quantity` (int), `outcome`, `details` | one audit record (FR-COM-003, FR-LOG-004); never contains a PIN or patient data | in ID order; every exchange while connected |
| ADC → MS | `heartbeat` | (none) | keeps the link alive when nothing else is pending | every `heartbeatPeriod` (1 s) when idle |
| MS → ADC | `ack` | `ids` (array of strings) | the listed message IDs are stored by the MS | in the reply to every exchange |
| MS → ADC | `config` | `id`, `key`, `value` (strings) | configuration update (C8); only `heartbeatPeriodMs` is accepted (tickPeriod ≤ value < commLossTimeout) | any reply; every update is logged by the ADC as CONFIG_UPDATED APPLIED / REJECTED |

Example (`event`):

```json
{"cabinet_id":"ADC-01","type":"event","id":"EVT-6","timestamp":"2026-11-04T05:12:55.858Z","event_id":6,"event_type":"DISPENSE_OUTCOME","user":"N100","medication":"M03","quantity":2,"outcome":"COMPLETED","details":"requested=2 removed=2 reason=drawer closed"}
```

## Timing and loss detection

- The ADC exchanges at least every `heartbeatPeriod` = **1 s** (retry period when failing), and
  every tick (200 ms) while it has unsent data and the last exchange succeeded.
- The ADC declares the link lost after **3 s** without a successful exchange
  (`commLossTimeout`, clarification C3), so loss is always detected within 5 s (NFR-PRF-003;
  measured 2.5 s live).
- Suggestion for Part B: declare the cabinet lost after 3 s without any message from it.

## Behaviour when the Monitoring System is unavailable (D1)

- Loss is detected within 5 seconds (NFR-PRF-003, FR-FAULT-003).
- The ADC keeps operating locally (FR-COM-004, NFR-AVL-001, NFR-AVL-004).
- Information that cannot be sent is stored locally (NFR-AVL-002, FR-LOG-004) and confirmed
  event records are never removed because of the outage (NFR-REL-002).
- Stored information is sent once the link is restored (NFR-AVL-003, FR-COM-005).

## Degraded behaviour

| Situation | ADC behaviour | Expected from Part B |
|---|---|---|
| Link down (no successful exchange for 3 s) | SM3 → DISCONNECTED; COMM_STATE_CHANGED and a non-critical CommunicationLoss fault logged; indicator WARNING "Monitoring link down: cabinet working locally"; mode unchanged; all outgoing information kept in a persisted outbox / event cursor; retry every 1 s | treat the cabinet as offline; do not assume anything was lost |
| Link restored (first successful exchange while down) | SM3 → RESYNCHRONIZING: re-sends full status and every inventory record plus everything unsent, events in ID order; when nothing is left → CONNECTED; CommunicationLoss cleared | accept a burst; deduplicate by `id` |
| Partner never seen since start-up | SM3 Connecting → DISCONNECTED after 3 s (C4); identical to "link down" | — |
| ADC restarts while messages are unsent | outbox and event cursor are reloaded from disk; sending resumes from the last acknowledged event | may receive messages it has already stored (if the ack was lost): deduplicate by `id` |
| Duplicate message | the ADC never re-uses an ID; it may re-send an ID whose ack was lost | store once, ack again (FakeMonitor does this and counts duplicates) |
| Out-of-order message | events are always sent in ID order; `status`/`inventory` carry their own timestamp and are latest-wins | order events by `event_id`; keep the newest `status` / `inventory` by `timestamp` |
| Ack for an unknown ID | ignored | — |
| Malformed inbound message | ignored (and an unknown/invalid `config` is rejected and logged) | — |

## Open questions for Part B

1. Transport: TCP socket, HTTP POST, MQTT, or something the course provides? Who connects to whom?
2. Is one flat JSON object per message acceptable, and in which framing (one message per line, an array per request)?
3. Is the field list above enough for the Monitoring System's displays and reports? Any field to add (e.g. medication names in events)?
4. Acknowledgement: per message (`ids` list, as proposed) or a single "stored up to" event ID?
5. Heartbeat and loss timing: is 1 s / 3 s acceptable on both sides?
6. Synchronization handshake: is "re-send everything unsent + full status/inventory, deduplicate by `id`" enough, or does Part B want an explicit sync-start / sync-end message?
7. Which configuration keys may the Monitoring System change, and with what ranges? (Part A proposes only `heartbeatPeriodMs`.)
8. Should the Monitoring System be able to request a full status/inventory report on demand?
9. Badges are sent as the `user` of events (no names, no PINs, no patient data). Is that acceptable to Part B and the hospital?
