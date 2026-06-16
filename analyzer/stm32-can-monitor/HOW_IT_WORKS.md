# STM32 CAN Latency Monitor — How It Works

## System Overview

```
STM32F439ZI (Nucleo-144)
┌──────────────────────────────────────────────────────┐
│  CAN1 TX ──────────────────────► CAN2 RX (loopback) │
│     │  pack(tx_ticks, seq)           │               │
│     │                         rx_ticks captured      │
│     │                         latency = rx - tx      │
│     │                         stats updated          │
│     └──────────────────────────────►printk(CSV line) │
└──────────────────────────────────────────────────────┘
          │ USB-CDC /dev/ttyACM0 @ 115200
          ▼
     Telegraf (Docker)
     inputs.execd: stty + grep '^CSV,'
     data_format = "csv"
          │
          ▼
     InfluxDB v2 (Docker)
     bucket: can_latency
     measurement: can_latency
          │
          ▼
     Grafana (Docker)
     Flux queries → 10 dashboard panels
     http://localhost:3000
```

---

## 1. Firmware (Zephyr RTOS on STM32F439ZI)

### Hardware used

| Resource     | Role |
|-------------|------|
| CAN1 (PD1/PD0) | Transmitter |
| CAN2 (PB13/PB12) | Receiver |
| TIM2 via Zephyr `counter2` | Free-running HW timer for µs timestamps |
| USER BUTTON (PC13) | Start / pause TX, print summary |
| UART3 → USB-CDC | Serial output to host at 115200 baud |

### Boot sequence (`main.c`)

```
main()
  ├─ timestamp_init()   — start TIM2 free-running counter
  ├─ can_handler_init() — start CAN1 TX + CAN2 RX with filter 0x123
  └─ button_init()      — configure USER BUTTON interrupt
         │
         └─ loop: every 50 ms
               if can_send_enabled → can_send_frame(seq++)
```

CAN sending is **OFF at boot**. Press the USER BUTTON once to start. Press again to pause and print a stats summary to the serial console.

### Timestamping (`timestamp.c`)

Uses the Zephyr `counter` API bound to `counter2` (TIM2 on STM32F4):

```c
get_hw_timestamp_ticks()   // raw 32-bit counter value
ticks_to_us(delta_ticks)   // converts tick delta → microseconds
```

TIM2 runs at the APB1 timer clock (up to 84 MHz on F439). Tick subtraction handles 32-bit wraparound via unsigned underflow.

### TX frame (`can_handler.c`)

```
frame.id  = 0x123
frame.dlc = 8
frame.data[0..3] = tx_ticks   (uint32, little-endian)
frame.data[4..7] = seq_num    (uint32, little-endian)
```

`tx_ticks` is captured **immediately before** `can_send()` to minimise measurement overhead.

### RX callback (`can_handler.c`)

```
can_rx_callback()
  1. rx_ticks = get_hw_timestamp_ticks()   ← captured first
  2. unpack tx_ticks, seq_num from frame.data
  3. latency_us = ticks_to_us(rx_ticks - tx_ticks)
  4. jitter_us  = stats_record(latency_us, seq_num)
  5. stats_get_snapshot(&snap)
  6. printk("CSV,seq,tx_ticks,rx_ticks,lat,jit,min,max,mean,stddev,total,lost\n")
```

### Statistics (`stats.c`)

All computed on-device in integer arithmetic — no FPU dependency.

| Statistic | Formula |
|-----------|---------|
| min / max | running min/max of `latency_us` |
| mean | `total_us / count` (integer) |
| jitter | `|latency_us - prev_latency_us|` per frame; mean jitter = `total_jitter / (count-1)` |
| **std dev** | `isqrt64(E[X²] - (E[X])²)` — sum-of-squares method, integer Newton's sqrt |
| packet loss | sequence-number gaps: `expected = last_seq + 1`, any jump counts as lost frames |

`isqrt64()` is a Newton's-method integer square root — runs in ~6 iterations, no `<math.h>` needed.

### CSV output format

Every received frame emits one line on the serial port:

```
CSV,<seq>,<tx_ticks>,<rx_ticks>,<latency_us>,<jitter_us>,<min_us>,<max_us>,<mean_us>,<stddev_us>,<total_frames>,<lost_frames>
```

Example:
```
CSV,42,1783420,1785614,1094,12,900,1300,1050,38,43,0
```

Only lines starting with `CSV,` are forwarded to InfluxDB. All other `printk()` output (boot banner, button events, stats summaries) is ignored by the pipeline.

---

## 2. Data Pipeline (Docker)

### Telegraf (`telegraf/telegraf.conf`)

```toml
[[inputs.execd]]
  command = ["sh", "-c",
    "stty -F /dev/ttyACM0 115200 raw -echo cs8 -cstopb -parenb &&
     grep --line-buffered '^CSV,' /dev/ttyACM0"]
  data_format = "csv"
  name_override = "can_latency"
  csv_column_names = [
    "_src", "seq", "tx_ticks", "rx_ticks", "latency_us", "jitter_us",
    "min_latency_us", "max_latency_us", "avg_latency_us", "stddev_us",
    "total_frames", "lost_frames"
  ]
  csv_tag_columns = ["_src"]   # "CSV" string becomes a tag, not a field
```

- `stty` configures the baud rate and puts the port in raw mode
- `grep --line-buffered` filters out everything except CSV lines before Telegraf sees them
- `execd` restarts the command automatically if the device disconnects

Each CSV line becomes one InfluxDB point in the `can_latency` measurement with all fields stored as **integers**.

### InfluxDB v2 (`influxdb:2.7`)

```
org:    stm32org
bucket: can_latency
token:  stm32-super-secret-token-change-me
```

Fields stored per point:

| Field | Unit | Description |
|-------|------|-------------|
| `seq` | — | Frame sequence number |
| `tx_ticks` / `rx_ticks` | ticks | Raw HW timer values |
| `latency_us` | µs | One-way CAN latency |
| `jitter_us` | µs | Frame-to-frame latency change |
| `min_latency_us` | µs | Running minimum |
| `max_latency_us` | µs | Running maximum |
| `avg_latency_us` | µs | Running mean |
| `stddev_us` | µs | Running population std dev |
| `total_frames` | count | Total RX frames |
| `lost_frames` | count | Total detected lost frames |

---

## 3. Grafana Dashboard

Dashboard auto-provisioned from `grafana/dashboards/can_latency.json`.

### Row 1 — Current values (stat panels)

| Panel | Query | Notes |
|-------|-------|-------|
| Min Latency | `last(min_latency_us)` | Green < 300 µs |
| Max Latency | `last(max_latency_us)` | Red > 1500 µs |
| Avg Latency | `last(avg_latency_us)` | Mean since boot |
| Std Dev | `last(stddev_us)` | Population σ, integer sqrt on-device |
| Frame Rate | `derivative(total_frames, unit:1s)` | Frames/sec received |
| Packet Loss % | `pivot(lost+total) → map(lost/(lost+total)*100)` | Red > 1 % |

### Row 2 — Counters & ranges

| Panel | Description |
|-------|-------------|
| Frames RX | Total frames received since boot |
| Frames Lost | Cumulative lost frame count |
| Latency Range | `max_latency_us − min_latency_us` |
| Peak Jitter | `max(jitter_us)` in selected time window |

### Row 3 — Time series

- **Latency & Jitter** — per-frame latency and jitter over time (full width)
- **Frame Rate & Std Dev** — FPS (derivative) on left axis, σ on right axis
- **Packet Loss % & Lost Frames** — loss % on left axis, cumulative lost count on right axis

### Derived fields (computed in Flux, not stored)

**Frame Rate:**
```flux
|> filter(fn: (r) => r._field == "total_frames")
|> derivative(unit: 1s, nonNegative: true)
```

**Packet Loss %:**
```flux
|> filter(fn: (r) => r._field == "lost_frames" or r._field == "total_frames")
|> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
|> map(fn: (r) => ({_time: r._time,
     _value: float(v: r.lost_frames) / float(v: r.lost_frames + r.total_frames) * 100.0}))
```

**Latency Range:**
```flux
|> filter(fn: (r) => r._field == "max_latency_us" or r._field == "min_latency_us")
|> pivot(...)
|> map(fn: (r) => ({_time: r._time, _value: r.max_latency_us - r.min_latency_us}))
```

---

## 4. Quick Start

### Build & flash firmware

```bash
cd CAN_Latency_Tester/apps/stm32_to_stm32/main_pipeline
west build -p always -b nucleo_f439zi .
west flash
```

### Start the Docker stack

```bash
cd analyzer/stm32-can-monitor
docker compose up -d
```

Open Grafana: **http://localhost:3000** (admin / admin)

### Start the test

Press the **USER BUTTON** (blue button) on the Nucleo board.
- First press → starts CAN TX at 20 frames/sec
- Second press → pauses TX and prints a full stats summary to serial

Data appears in Grafana within 1–2 seconds.

### Clear old data (schema reset)

If InfluxDB reports a field type conflict (e.g. after switching parsers):

```bash
# delete bucket
docker exec stm32_influxdb influx bucket delete \
  --name can_latency --org stm32org \
  --token stm32-super-secret-token-change-me

# recreate bucket
docker exec stm32_influxdb influx bucket create \
  --name can_latency --org stm32org --retention 30d \
  --token stm32-super-secret-token-change-me

docker compose restart telegraf
```

---

## 5. File Map

```
analyzer/stm32-can-monitor/
├── docker-compose.yml               # 3 services: influxdb, telegraf, grafana
├── telegraf/telegraf.conf           # serial → CSV → InfluxDB pipeline
├── grafana/
│   ├── provisioning/
│   │   └── datasources/influxdb.yml # auto-wires InfluxDB datasource
│   └── dashboards/can_latency.json  # 10-panel dashboard (auto-loaded)
└── HOW_IT_WORKS.md                  # this file

apps/stm32_to_stm32/main_pipeline/src/
├── main.c          # boot, 50 ms TX loop
├── can_handler.c   # CAN1 TX, CAN2 RX callback, CSV printk
├── stats.c/h       # min/max/mean/stddev/jitter/loss (integer only)
├── timestamp.c/h   # TIM2 via Zephyr counter API
└── button.c/h      # USER BUTTON → start/pause + stats print
```
