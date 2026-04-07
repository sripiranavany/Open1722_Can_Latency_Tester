# Open1722 CAN Latency Test — Architecture Diagram

```mermaid
flowchart TD
    subgraph FIXED["Fixed Hardware (identical in both runs)"]
        SRC["RPi4 #1 + MCP2515 HAT\n━━━━━━━━━━━━━━━━\nCAN SOURCE\nPeriodic frames @ 500kbps\nSeq# in payload"]
        UCAN["UCAN (passive sniffer)\n━━━━━━━━━━━━━━━━\nHardware timestamp T1\nwhen frame hits bus"]
    end

    subgraph RUN1["Run 1 — Linux Gateway"]
        GW_LINUX["RPi4 #2 + MCP2515 HAT\n━━━━━━━━━━━━━━━━\nOpen1722 on Linux\nSocketCAN → AVTP PDU\nRaw Ethernet socket"]
    end

    subgraph RUN2["Run 2 — Zephyr Gateway"]
        GW_ZEPHYR["STM32F439ZI + SN65HVD230\n━━━━━━━━━━━━━━━━\nOpen1722 on Zephyr\nbxCAN ISR → k_msgq\n→ AVTP PDU → LAN8742A"]
    end

    subgraph PC["PC / Laptop"]
        RX["IEEE-1722 Receiver\nTimestamp T2 via SO_TIMESTAMPING"]
        ANALYSIS["Analysis\n━━━━━━━━━━━━━━━━\nLatency = T2 − T1\nJitter = stddev(T2−T1)\nMin / Max / Mean"]
        CANDUMP["candump --log\nUCAN timestamps"]
    end

    SRC -->|"CAN Bus 500kbps"| GW_LINUX
    SRC -->|"CAN Bus 500kbps"| GW_ZEPHYR
    SRC -->|"CAN Bus tap"| UCAN

    GW_LINUX -->|"Ethernet IEEE-1722"| RX
    GW_ZEPHYR -->|"Ethernet IEEE-1722"| RX

    UCAN -->|USB| CANDUMP
    RX --> ANALYSIS
    CANDUMP --> ANALYSIS
```

## Notes

- **RPi4 #1 and UCAN** are fixed across both runs — only the gateway node swaps.
- **T1** = UCAN hardware timestamp when the CAN frame appears on the bus.
- **T2** = PC timestamp when the IEEE-1722 Ethernet frame arrives (`SO_TIMESTAMPING`).
- **Latency** = T2 − T1 per packet; **Jitter** = stddev across N packets.
- Both runs use the same PC receiver code — only the gateway side changes.
