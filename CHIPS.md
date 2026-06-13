# Chips & Hardware Reference

**License:** GPL-3.0

Hardware selection for the "In-Slot" Rumble Cartridge. The goal is to fit a full haptic system inside the ~2mm thickness of a game card.

---

## 1. Physical Constraints

| Property | Value | Note |
| :--- | :--- | :--- |
| Width | 21 mm | Standard cart width. |
| Thickness | ~2.1 mm | **The main challenge.** Requires WLCSP/BGA packages. |
| Actuator | iPhone 12 Taptic | Fits if the shell is extended "up" into the slot cavity. |

---

## 2. Pinout & Electrical

The 17-pin interface is our gateway. 
- **VCC 3.1V (Pin 8):** Main power. We'll use this for the motor, buffered by a **supercapacitor**.
- **VCC 1.8V (Pin 11):** Logic power for the FPGA/MCU.
- **DAT0-DAT7 (Pins 6-15):** Our hijacked data bus for rumble values.

---

## 3. Component Selection

### 3.1 Bus Controller (FPGA)
We need a chip to listen to the DAT lines and decode our custom protocol.
- **Target:** **Lattice iCE40 UltraLite (UL1K)**. 
- **Size:** 1.4 x 1.4 mm.
- **Why:** Smallest FPGA available, native 1.8V I/O, open-source toolchain support.

### 3.2 Haptic Driver
- **Target:** **TI DRV2605L**.
- **Package:** DSBGA (1.5 x 1.5 mm).
- **Function:** Drives the Taptic Engine with closed-loop auto-resonance. This is critical for power efficiency.

### 3.3 The Actuator
- **Target:** **iPhone 12 Taptic Engine**.
- **Why:** Best-in-class linear vibration. It provides the "HD" feel that simple coin motors lack.

---

## 4. Power Management

The slot cannot provide the >300mA needed for a Taptic "thump".
- **Solution:** A low-profile **supercapacitor** (e.g., 0.1F). 
- **Logic:** The slot trickle-charges the cap during idle; the cap provides the burst current for the motor.

---

## 5. Proposed Block Diagram

```
[ Switch Slot ]
      │ (1.8V Logic / 3.1V Power)
      ▼
[ iCE40 FPGA ] ◄── Hijacks DAT lines for Rumble Data
      │ (I2C / PWM)
      ▼
[ DRV2605L Driver ] ◄── Powered by Supercap
      │
      ▼
[ Taptic Engine ]
```

---
*Hardware specs by Dimasick-git.*
