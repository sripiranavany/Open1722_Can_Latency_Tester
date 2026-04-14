# CAN ↔ Ethernet Bridge using Open1722 (Raspberry Pi)

This project uses **COVESA Open1722** to bridge CAN messages over Ethernet between two Raspberry Pi devices.

It enables:
- CAN → Ethernet (AVTP)
- Ethernet → CAN

---

# 🧠 System Overview

```

Pi1 (CAN) → Open1722 → Ethernet → Open1722 → Pi2 (CAN)

````

---

# ⚙️ Requirements

- Raspberry Pi 1 & Pi 2
- CAN HAT (SocketCAN support)
- Ethernet connection between Pis
- Open1722 built and installed
- CAN interface enabled (`can0` or `can1`)

---

# 🔧 CAN Interface Setup (Both Pis)

Create systemd service:

```bash
sudo nano /etc/systemd/system/can1.service
````

### Service file:

```ini
[Unit]
Description=Bring up CAN1 interface
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/sbin/ip link set can1 up type can bitrate 250000
ExecStop=/sbin/ip link set can1 down
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

### Enable service:

```bash
sudo systemctl daemon-reexec
sudo systemctl enable can1.service
```

---

# 🌐 Open1722 Bridge Setup

## Pi1 (CAN → Ethernet)

```bash
sudo nano /etc/systemd/system/acf-can-bridge.service
```

```ini
[Unit]
Description=Open1722 CAN Bridge (Pi1)
After=can1.service network-online.target
Wants=can1.service network-online.target

[Service]
Type=simple
ExecStart=/home/pi/Open1722/build/examples/acf-can/linux/acf-can-bridge \
  --dst-addr <PI2_MAC> \
  -i eth0 \
  --canif can1

Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

---

## Pi2 (Ethernet → CAN)

```ini
[Unit]
Description=Open1722 CAN Bridge (Pi2)
After=can1.service network-online.target
Wants=can1.service network-online.target

[Service]
Type=simple
ExecStart=/home/pi/Open1722/build/examples/acf-can/linux/acf-can-bridge \
  --dst-addr <PI1_MAC> \
  -i eth0 \
  --canif can1

Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

---

# 🚀 Enable services (Both Pis)

```bash
sudo systemctl daemon-reexec
sudo systemctl enable acf-can-bridge.service
sudo systemctl start acf-can-bridge.service
```

---

# 🔍 Verification

## Check CAN traffic:

```bash
candump can1
```

## Check Ethernet AVTP packets:

```bash
sudo tcpdump -i eth0 ether proto 0x22f0
```

## Check service status:

```bash
systemctl status acf-can-bridge.service
```

---

# ⚠️ Important Notes

* MAC addresses must be set correctly:

  * Pi1 → Pi2 MAC
  * Pi2 → Pi1 MAC
* CAN bitrate must match all nodes (e.g., 250000)
* CAN interface name must match (`can1`)
* Ethernet must be connected before bridge starts

---

# 🧪 Test

### On Pi1:

```bash
cansend can1 123#11223344
```

### On Pi2:

```bash
candump can1
```

---

# 🎯 Result

✔ Automatic CAN ↔ Ethernet bridging after boot
✔ No manual setup required
✔ Stable AVTP communication using Open1722
