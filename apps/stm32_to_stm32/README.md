# CAN-over-Ethernet Latency Tester

A three-board latency measurement pipeline using **Zephyr OS**, **IEEE 1722 AVTP** (via Open1722), and **CAN bus** on STM32F439ZI Nucleo-144 boards.

---

## Overview

The system measures the full round-trip latency of a CAN message being:
1. Sent over CAN bus
2. Bridged to raw Ethernet using AVTP (IEEE 1722)
3. Returned back over CAN bus

```
┌─────────────────┐   CAN Bus (250 kbps)   ┌─────────────────┐
│  main_pipeline  │ ─────────────────────► │    Board 2      │
│  (Nucleo F439ZI)│                         │  (Nucleo F439ZI)│
│                 │ ◄─────────────────────  │                 │
│  CAN2 RX (PB5) │   CAN Bus (250 kbps)    │  CAN1 TX (PD1) │
└─────────────────┘                         └────────┬────────┘
         ▲                                           │
         │                                    AVTP over Ethernet
         │                                    (IEEE 1722 / 0x22F0)
         │                                           │
         │                                    ┌──────▼────────┐
         │      CAN Bus (250 kbps)             │    Board 3    │
         └──────────────────────────────────── │ (Nucleo F439ZI│
                                               │               │
                                               │ Console (UART)│
                                               └───────────────┘
```

---

## Boards and Roles

| Board | Role | App |
|-------|------|-----|
| **main_pipeline** | CAN talker + latency calculator | `apps/test/main_pipeline` |
| **Board 2** | CAN → AVTP Ethernet bridge | `apps/test/board2` |
| **Board 3** | AVTP Ethernet → CAN return bridge + console | `apps/test/board3` |

---

## Hardware Connections

### CAN Bus Wiring

> All CAN segments require a **120 Ω termination resistor** between CANH and CANL at each end.

#### Segment 1 — main_pipeline CAN1 → Board 2 CAN1

| Signal | main_pipeline pin | Board 2 pin |
|--------|------------------|-------------|
| CAN1 TX | PD1 (CN9) | PD0 (RX) |
| CAN1 RX | PD0 (CN9) | PD1 (TX) |

> Connect main_pipeline **PD1 → Board2 PD0** and **PD0 → Board2 PD1** (cross-connect TX↔RX).

#### Segment 2 — Board 3 CAN1 → main_pipeline CAN2

| Signal | Board 3 pin | main_pipeline pin |
|--------|-------------|------------------|
| CAN1 TX | PD1 | PB5 (CAN2 RX) |
| CAN1 RX | PD0 | PB6 (CAN2 TX) |

> Connect Board3 **PD1 → main_pipeline PB5** and **PD0 → main_pipeline PB6**.

### Ethernet

| From | To | Cable |
|------|----|-------|
| Board 2 RJ45 | Board 3 RJ45 | Direct Ethernet cable (crossover or auto-MDI) |

---

## Software Architecture

### main_pipeline

**What it does:**
- Initialises CAN1 (TX) and CAN2 (RX)
- Every 100 ms, reads a hardware timer (`get_hw_timestamp_us()`) and sends a CAN frame on CAN1
- CAN2 listens for the returning frame, calculates latency and jitter, and prints CSV logs

**CAN TX frame format (ID = 0x123, DLC = 8):**

```
Byte 0..3  │  tx_timestamp_us  │  uint32_t, little-endian
Byte 4..7  │  seq_num          │  uint32_t, little-endian
```

**CAN RX callback (CAN2):**
```c
latency_us = rx_timestamp_us - tx_timestamp_us;
jitter_us  = |latency_us - previous_latency_us|;
// Prints: CSV,seq,tx_ts,rx_ts,latency,jitter
```

---

### Board 2 — CAN → AVTP Ethernet Bridge

**Source layout:**

| File | Responsibility |
|------|---------------|
| `main.c` | Init calls + CAN RX → AVTP TX loop |
| `can_handler.c/h` | CAN1 init, RX filter, message queue |
| `net_handler.c/h` | Ethernet link wait, socket create + destination address |
| `avtp_handler.c/h` | `avtp_build_frame()` — builds NTSCF/GPC PDU from CAN frame |

**What it does:**
1. Listens on **CAN1** for frames with ID `0x123` (250 kbps, PD0/PD1)
2. On every received CAN frame, builds an **AVTP NTSCF** packet containing the CAN payload
3. Sends the AVTP frame as a raw Ethernet frame (EtherType `0x22F0`) to the IEEE 1722 multicast MAC

**AVTP frame structure on the wire:**

```
┌──────────────────────────────────────────────────────────────┐
│  Ethernet Header (14 bytes)                                  │
│    Dest MAC: 01:1B:19:00:00:00  (IEEE 1722 multicast)        │
│    Src  MAC: Board2 MAC                                      │
│    EtherType: 0x22F0 (ETH_P_TSN / AVTP)                     │
├──────────────────────────────────────────────────────────────┤
│  AVTP NTSCF Header (12 bytes)                                │
│    Subtype:     0x82 (NTSCF)                                 │
│    Stream ID:   0xAABBCCDDEEFF0001                           │
│    Sequence:    0, 1, 2, ...                                 │
│    Data Length: length of ACF payload below                  │
├──────────────────────────────────────────────────────────────┤
│  ACF GPC Header (8 bytes)                                    │
│    ACF Msg Type: GPC (General Purpose Control)               │
│    GPC Msg ID:   0x01                                        │
│    ACF Length:   (quadlets)                                  │
├──────────────────────────────────────────────────────────────┤
│  CAN Payload (13 bytes, padded to 16)                        │
│    Byte 0..3  │ CAN ID (uint32_t)                           │
│    Byte 4     │ DLC                                          │
│    Byte 5..12 │ CAN data [ts_us (4B)][seq_num (4B)]         │
└──────────────────────────────────────────────────────────────┘
```

**Key config:**
- Socket: `AF_PACKET, SOCK_DGRAM, ETH_P_TSN`
- CAN receive: `k_msgq_get()` blocks on `CAN_MSGQ` until frame arrives
- No IP stack needed — raw Ethernet only

---

### Board 3 — AVTP Ethernet → CAN Return Bridge

**Source layout:**

| File | Responsibility |
|------|---------------|
| `main.c` | Init calls + AVTP RX → CAN TX loop |
| `can_handler.c/h` | CAN1 init, `can_send_return()` |
| `net_handler.c/h` | Ethernet link wait, socket create + bind |
| `avtp_handler.c/h` | `avtp_parse_frame()` — validates NTSCF/GPC and extracts payload |

**What it does:**
1. Binds an `AF_PACKET` socket to `ETH_P_TSN` (0x22F0) on the Ethernet interface
2. Receives every AVTP NTSCF frame, validates stream ID `0xAABBCCDDEEFF0001`
3. Parses the GPC ACF payload to extract `tx_timestamp_us` and `seq_num`
4. Prints received data to the **UART console** (picocom)
5. Sends a CAN frame back to **main_pipeline CAN2** carrying the original `tx_timestamp_us` and `seq_num`

**CAN return frame (ID = 0x123, DLC = 8) — same format as original:**
```
Byte 0..3  │  tx_timestamp_us  │ original timestamp from main_pipeline
Byte 4..7  │  seq_num          │ original sequence number
```

This allows main_pipeline to calculate the **full end-to-end latency** including:
- CAN1 TX time
- Board 2 CAN receive + AVTP build time
- Ethernet propagation
- Board 3 AVTP receive + CAN TX time
- CAN2 propagation

---

## Data Flow Step by Step

```
Step 1: main_pipeline reads hardware timer → ts_us = 34569711 µs
        Sends CAN frame: ID=0x123, data=[ts_us][seq=0]

Step 2: Board 2 CAN1 RX fires via message queue
        Packs CAN data into AVTP NTSCF + GPC frame
        Sends raw Ethernet frame → EtherType 0x22F0

Step 3: Board 3 AF_PACKET socket receives AVTP frame
        Validates NTSCF: subtype=0x82, stream_id=0xAABBCCDDEEFF0001
        Parses GPC payload → ts_us=34569711, seq=0
        Prints to console (picocom)
        Sends CAN frame: ID=0x123, data=[ts_us][seq=0] → main_pipeline CAN2

Step 4: main_pipeline CAN2 RX callback fires
        rx_ts = get_hw_timestamp_us()
        latency = rx_ts - tx_ts = end-to-end round-trip latency
        Prints: CSV,0,34569711,34571234,1523,12
```

---

## Build Instructions

### Prerequisites

```bash
# Activate the Zephyr virtual environment
source /path/to/zephyr/.zephyr/bin/activate

# Verify west is available
west --version
```

### Build main_pipeline

```bash
cd apps/test/main_pipeline
west build -p always -b nucleo_f439zi_devkit .
west flash
```

### Build Board 2 (CAN → AVTP bridge)

```bash
cd apps/test/board2
west build -p always -b nucleo_f439zi .
west flash
```

### Build Board 3 (AVTP → CAN return + console)

```bash
cd apps/test/board3
west build -p always -b nucleo_f439zi .
west flash
```

---

## Monitoring with picocom

Each board exposes a UART console via the ST-LINK USB connector (USART3, PD8/PD9, 115200 baud).

```bash
# Find the ports
ls /dev/ttyACM*

# Open Board 3 console (adjust port as needed)
picocom -b 115200 /dev/ttyACM0

# Open main_pipeline console (in another terminal)
picocom -b 115200 /dev/ttyACM1
```

---

## Expected Console Output

### Board 3 console

```
[00:00:03.001,000] <inf> avtp_listener: Waiting for Ethernet link...
[00:00:03.210,000] <inf> avtp_listener: Ethernet link up
[00:00:03.211,000] <inf> avtp_listener: AVTP listener + CAN return bridge ready
[00:00:15.926,000] <inf> avtp_listener: [RX] avtp_seq=0 can_id=0x123 can_seq=0 tx_ts=34569711 us
[00:00:15.926,000] <inf> avtp_listener: [TX] CAN return: seq=0 ts=34569711 us
[00:00:16.927,000] <inf> avtp_listener: [RX] avtp_seq=1 can_id=0x123 can_seq=1 tx_ts=35570313 us
[00:00:16.927,000] <inf> avtp_listener: [TX] CAN return: seq=1 ts=35570313 us
```

### main_pipeline console

```
==========================================
    CAN Latency Tester - Initialized
==========================================
  Send Interval: 100 ms
==========================================
[TX] Seq=0 @ 34569711 us
[TX] Seq=1 @ 35570313 us
CSV,0,34569711,34571234,1523,0
CSV,1,35570313,35571890,1577,54
```

> **CSV columns:** `seq, tx_timestamp_us, rx_timestamp_us, latency_us, jitter_us`

---

## Configuration Reference

### AVTP Stream ID

Defined in both `board2/src/avtp_handler.h` and `board3/src/avtp_handler.h`:

```c
#define STREAM_ID  0xAABBCCDDEEFF0001ULL
```

Both must match. Board 3 filters incoming AVTP frames by this stream ID.

### CAN Parameters

| Parameter | Value | Where |
|-----------|-------|-------|
| CAN ID | `0x123` | all boards |
| Bus speed | 250 kbps | all boards |
| Sample point | 87.5% | all boards |
| CAN1 pins | PD0 (RX), PD1 (TX) | board2, board3 |
| CAN2 pins | PB5 (RX), PB6 (TX) | main_pipeline only |

### Ethernet Parameters

| Parameter | Value |
|-----------|-------|
| EtherType | `0x22F0` (ETH_P_TSN) |
| Dest MAC | `01:1B:19:00:00:00` (IEEE 1722 multicast) |
| Protocol | AVTP NTSCF + ACF GPC |
| IP layer | None — raw Ethernet only |

---

## Kconfig Summary

| Option | board2 | board3 | main_pipeline |
|--------|--------|--------|---------------|
| `CONFIG_NETWORKING` | y | y | n |
| `CONFIG_NET_L2_ETHERNET` | y | y | n |
| `CONFIG_ETH_STM32_HAL` | y | y | n |
| `CONFIG_NET_SOCKETS_PACKET` | y | y | n |
| `CONFIG_POSIX_API` | y | y | n |
| `CONFIG_CAN` | y | y | y |
| `CONFIG_NET_MGMT_EVENT` | y | y | n |

---

## Open1722 Integration

Both board2 and board3 compile Open1722 source files directly into the app via `CMakeLists.txt`:

```cmake
get_filename_component(OPEN1722_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Open1722" ABSOLUTE)

file(GLOB_RECURSE OPEN1722_SOURCES "${OPEN1722_DIR}/src/*.c")

target_sources(app PRIVATE
    src/main.c
    src/can_handler.c
    src/net_handler.c
    src/avtp_handler.c
    ${OPEN1722_SOURCES}
)
target_include_directories(app PUBLIC ${OPEN1722_DIR}/include)
```

**Headers used:**

| Header | Purpose |
|--------|---------|
| `avtp/acf/Ntscf.h` | AVTP NTSCF frame init, set/get fields |
| `avtp/acf/Gpc.h` | ACF General Purpose Control payload |

---

## Troubleshooting

### Board 3 receives nothing
- Check Ethernet cable is plugged in between board2 and board3
- Verify both boards show `Ethernet link up` in their consoles
- Confirm board2 is receiving CAN frames: `[CAN RX]` lines should appear on board2 console

### main_pipeline receives no CAN frames back
- Check CAN wiring between board3 PD1 and main_pipeline PB5
- Verify board3 shows `[TX] CAN return` lines in console
- Ensure 120 Ω termination resistors are on the CAN bus

### AVTP frames are received but stream ID mismatch
- Both `board2/src/avtp_handler.h` and `board3/src/avtp_handler.h` must define the same `STREAM_ID`
- Default: `0xAABBCCDDEEFF0001ULL`
