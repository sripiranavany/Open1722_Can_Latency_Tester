# Open1722 CAN Latency Test — Physical Connection Diagram

```mermaid
flowchart TD
    subgraph CAN_BUS["━━━━━━━━━━  CAN Bus (500 kbps)  ━━━━━━━━━━"]
        direction LR
        T1["120Ω\nTerminator"]
        CANLINE["──── CAN H / CAN L ────"]
        T2["120Ω\nTerminator"]
        T1 --- CANLINE --- T2
    end

    subgraph NODE1["Node 1 — CAN Source (always active)"]
        RPI1["RPi4 #1\n(RPi OS Lite)"]
        HAT1["MCP2515 CAN HAT\n(SPI → CAN)"]
        RPI1 -- SPI --> HAT1
    end

    subgraph NODE2["Node 2 — Linux Gateway (Run 1 only)"]
        RPI2["RPi4 #2\n(RPi OS Lite)\nOpen1722 on Linux"]
        HAT2["MCP2515 CAN HAT\n(SPI → CAN)"]
        RPI2 -- SPI --> HAT2
    end

    subgraph NODE3["Node 3 — Zephyr Gateway (Run 2 only)"]
        STM["STM32F439ZI\n(Nucleo-F439ZI)\nOpen1722 on Zephyr"]
        XCVR["SN65HVD230\nCAN Transceiver\n(TX/RX ↔ H/L)"]
        STM -- "bxCAN TX/RX" --> XCVR
    end

    subgraph SNIFFER["Passive Sniffer (always active)"]
        UCAN["UCAN\nCAN ↔ USB adapter\nHW timestamp T1"]
    end

    subgraph LAPTOP["PC / Laptop"]
        direction TB
        USB_IN["USB port\ncandump --log\n(T1 timestamps)"]
        ETH_IN["Ethernet port\nIEEE-1722 Receiver\nSO_TIMESTAMPING T2"]
        CALC["Analysis Script\nLatency = T2 − T1\nJitter = stddev"]
        USB_IN --> CALC
        ETH_IN --> CALC
    end

    subgraph SWITCH["Ethernet Switch"]
        SW["100/1000 Mbps Switch"]
    end

    HAT1  -- "CAN H/L" --> CAN_BUS
    HAT2  -- "CAN H/L" --> CAN_BUS
    XCVR  -- "CAN H/L" --> CAN_BUS
    UCAN  -- "CAN H/L" --> CAN_BUS

    UCAN  -- "USB" --> USB_IN

    RPI2  -- "Ethernet" --> SW
    STM   -- "Ethernet\n(LAN8742A PHY)" --> SW
    SW    -- "Ethernet" --> ETH_IN
```

## Notes

- All 4 CAN nodes share the same CAN H/L wire with **120Ω terminators at both physical ends**.
- **UCAN → USB → Laptop** is always connected — it passively sniffs and records T1.
- The **Ethernet switch** connects both gateways to the laptop; only one gateway is active per run.
- **STM32** uses the onboard LAN8742A PHY via the Nucleo-F439ZI RJ45 connector.
- **RPi4 #1** connects to the CAN bus only — no Ethernet needed on the source node.
