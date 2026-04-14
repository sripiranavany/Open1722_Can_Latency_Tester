# ✅ Open1722 CAN ↔ Ethernet Bridge Setup

## Overview

This setup uses **Open1722** to bridge CAN messages over Ethernet between two Raspberry Pi devices.

---

# 🟢 Pi1 (CAN → Ethernet sender)

Pi1 receives CAN frames from the Nucleo board and transmits them over Ethernet.

```bash
sudo ./acf-can-bridge \
  --dst-addr <PI2_ETHERNET_MAC_ADDRESS> \
  -i eth0 \
  --canif can1
```

### Interface description:

* `eth0` → Ethernet interface used for AVTP transmission
* `can1` → CAN interface receiving frames from Nucleo board
* `--dst-addr` → **MAC address of Pi2 (receiver device)**

---

# 🔵 Pi2 (Ethernet → CAN receiver)

Pi2 receives AVTP frames from Pi1 and injects them into CAN bus.

```bash
sudo ./acf-can-bridge \
  --dst-addr <PI1_ETHERNET_MAC_ADDRESS> \
  -i eth0 \
  --canif can1
```

### Interface description:

* `eth0` → Ethernet interface receiving AVTP frames
* `can1` → CAN interface output to local CAN bus
* `--dst-addr` → MAC address used for return/stream mapping (Pi1)

---

# ⚠️ Important Notes

* Each Pi must use the **correct destination MAC address of the opposite node**
* `can0` or `can1` depends on your system configuration (`ip link`)
* CAN interfaces must be **UP before starting the bridge**
* Bitrate must match across all CAN nodes (Pi + Nucleo)

---

# 🔍 Verification

## Check Ethernet AVTP traffic:

```bash
sudo tcpdump -i eth0 -nn -e ether proto 0x22f0
```

## Check CAN reception:

```bash
candump can1
```

---

# 🧠 Key Concept

```text
Pi1 CAN → Open1722 → Ethernet → Pi2 CAN
Pi2 CAN → Open1722 → Ethernet → Pi1 CAN
```

Each node bridges **its local CAN interface to the remote MAC address**.
