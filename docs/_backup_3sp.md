## **Project: Open1722 CAN Latency Tester: Linux vs. Zephyr IEEE-1722 Gateway**

### **Team**

| Name | Role | Date | Signature |
| :--- | :--- | :--- | :--- |
| Sripiranavan Yogarajah | Author / Developer | | |
| Sebastian Schildt | Client | | |
| Ulrich Straus | Client | | |

### **Motivation**
In modern automotive and industrial systems, CAN data often must be transported over Ethernet backbones using time-sensitive networking protocols. The **IEEE-1722 (AVTP)** standard defines how to encapsulate CAN frames into Ethernet packets. **Open1722** is an open-source implementation of IEEE-1722 that runs on both **Linux** and **Zephyr OS**, making it an ideal candidate for comparison.

Every gateway introduces **latency** and — critically — **jitter**, which can destabilize time-sensitive control loops. This project builds a measurement framework to quantify and compare the end-to-end latency and jitter of CAN forwarding through an IEEE-1722 bridge on both a Linux host and a Zephyr (STM32F439ZI) node, providing a real-world "performance delta" between a general-purpose OS and an RTOS.

### **Goals**
* Implement a **low-jitter CAN source** on RPi4 #1 (Linux) that generates CAN frames with predictable timing using `timerfd_create()` and `SCHED_FIFO`.
* Deploy **Open1722** on both a Linux node (Raspberry Pi / PC) and the STM32F439ZI running Zephyr to bridge CAN over Ethernet via IEEE-1722.
* Measure and compare **end-to-end latency** and **jitter** of the forwarding path for both implementations.
* Identify the dominant sources of jitter in each implementation (OS scheduling, driver buffering, network stack).

### **Requirements**
* **CAN Source:** A low-jitter CAN transmitter running on **RPi4 #1** (RPi OS Lite) using `timerfd_create()` with a `SCHED_FIFO` thread and CPU pinning, generating periodic frames at a defined bitrate (500 kbps).
* **IEEE-1722 Gateway (Zephyr):** STM32F439ZI running Open1722 on Zephyr — receives CAN frame, encapsulates into IEEE-1722 AVTP over Ethernet, transmits.
* **IEEE-1722 Gateway (Linux):** Raspberry Pi or Linux PC running Open1722 on Linux — same forwarding function for direct comparison.
* **Timestamp Capture:** Hardware-level RX/TX timestamps recorded at each stage (CAN ingress, Ethernet egress, final CAN egress) using a shared time reference (e.g., PTP or a logic analyzer).
* **Jitter Minimization:** Investigate techniques to reduce jitter on the CAN source — hardware-triggered TX, pinned CPU affinity (Linux), and thread priority tuning (Zephyr).
* **Diagnostic Interface:** UART shell or serial log reporting per-packet latency, mean, min, max, and standard deviation of jitter.

### **Solution Approach**
The system uses a **three-node pipeline** for each test run:

    [CAN Source Node] --CAN--> [Gateway Node] --Ethernet (IEEE-1722)--> [CAN Sink / Analyzer]

1. **CAN Source:** Generates CAN frames at a fixed interval using a hardware timer ISR to minimize scheduling jitter. A sequence counter is embedded in the payload for reordering/loss detection.
2. **Gateway — Zephyr (STM32F439ZI):** Receives CAN via interrupt-driven callback → pushes frame into `k_msgq` → forwarding thread encapsulates via Open1722 into an IEEE-1722 PDU → sends over Ethernet.
3. **Gateway — Linux (Raspberry Pi / PC):** Same pipeline using SocketCAN (`can0`) and Open1722's Linux-native POSIX socket backend.
4. **Measurement:** A **UCAN** (USB-CAN adapter) passively sniffs the CAN bus and provides hardware-level T1 timestamps via `candump --log` on the laptop. The PC IEEE-1722 receiver records T2 via `SO_TIMESTAMPING`. Latency = T2 − T1 per packet; jitter = stddev across N packets.

Both gateway implementations are tested under identical bus conditions. Results are compared to quantify the determinism advantage of Zephyr over Linux.

### **Roadmap**
| Task | Start Date | End Date |
| :--- | :--- | :--- |
| Hardware Setup & CAN Bus Verification | March 30, 2026 | March 31, 2026 |
| CAN TX/RX Basics on Linux (SocketCAN) and Zephyr | April 1, 2026 | April 3, 2026 |
| Low-Jitter CAN Source Implementation | April 4, 2026 | April 7, 2026 |
| Open1722 Integration — Linux Gateway | April 8, 2026 | April 12, 2026 |
| Open1722 Integration — Zephyr Gateway (STM32) | April 13, 2026 | April 20, 2026 |
| **Review 1: Basic IEEE-1722 Forwarding (Both Nodes)** | April 21, 2026 | April 22, 2026 |
| Timestamp & Latency Measurement Framework | April 23, 2026 | May 5, 2026 |
| Jitter Analysis & Source Optimization | May 6, 2026 | May 15, 2026 |
| Stress Testing (High Bus Load, Burst Traffic) | May 16, 2026 | May 25, 2026 |
| **Review 2: Data Analysis & Linux vs. Zephyr Comparison** | May 26, 2026 | May 27, 2026 |
| Final Documentation & Presentation | May 28, 2026 | June 15, 2026 |

### **Impediments and Measures**

**1. Jitter from Linux OS Scheduler**
* *Risk:* Linux is not an RTOS; thread preemption causes unpredictable delays in the forwarding path, making fair comparison difficult.
* *Measure:* Use `SCHED_FIFO` with `mlockall()` on Linux to minimize scheduling jitter. Use `chrt` and CPU pinning (`taskset`) to isolate the forwarding thread.

**2. Shared Time Reference for Cross-Node Timestamps**
* *Risk:* Without a common clock, comparing TX timestamps (source) with RX timestamps (sink) on different nodes is inaccurate.
* *Measure:* Use the **UCAN** (USB-CAN adapter connected to the laptop) as a ground-truth T1 reference — it provides hardware timestamps directly on the CAN bus, independent of both gateway nodes.

**3. Open1722 Zephyr Port Maturity**
* *Risk:* The Zephyr port of Open1722 may be less mature than the Linux version, introducing additional bugs or missing features.
* *Measure:* Start with the Linux port to validate the pipeline end-to-end first, then port to Zephyr incrementally with the same test vectors.

**4. Bus Congestion (Overrun Errors)**
* *Risk:* At high bus load, the gateway cannot clear the RX FIFO fast enough, causing dropped frames.
* *Measure:* Use CAN hardware filtering and Zephyr's `k_msgq` for deep buffering; monitor TEC/REC counters via UART shell.