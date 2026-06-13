# Rumble mod for Nintendo Switch Lite — concept & architecture

*Working document for discussion. Version 1.*

> **Note:** this is an earlier concept where the external path was considered primary.
> The project's main goal is now the **in-slot cartridge** (see
> [DESIGN-NOTES.md](./DESIGN-NOTES.md) §0), with external USB-C/BT as the fallback. The
> text below is kept as-is for history.

---

## 1. The idea and the main task

The Switch Lite has no built-in vibration (no HD motors in the body). The goal is to
bring back tactile feedback through a **plug-and-play device with an actuator** that
receives real rumble commands from the game and plays them — without opening the
console or modifying its internals.

The key principle the whole document rests on: **this doesn't restore a "removed"
feature — it intercepts a signal the console already computes but has nowhere to send.**

---

## 2. Why the Lite can vibrate at all (the key technical fact)

The rumble pipeline in the firmware (HOS) on the Lite is **not removed**. It has to
exist, because the Lite works normally with external Joy-Con and Pro Controllers that
have vibration, and games send it to them.

So the **hid** subsystem computes vibration values as usual. Only the actuator in the
built-in body is physically missing. The signal is live — it can be caught.

---

## 3. Recommended architecture

```
Game
  │  (sends vibration values to the active player)
  ▼
hid (firmware subsystem)
  │  ← INTERCEPTION POINT: the sysmodule reads the values here,
  │     BEFORE the system drops them for the missing motor
  ▼
Custom sysmodule (Atmosphere / libnx)
  │  forwards the values over the chosen channel
  ▼
Channel: USB-C  OR  Bluetooth
  ▼
External device (in a cartridge shell or a dongle)
  ├─ microcontroller (ESP32-class) — receives commands
  ├─ actuator driver
  └─ actuator (e.g. iPhone 12 Taptic Engine)
       power: USB-C passthrough OR its own battery
```

From the outside it's "plug it in and it works." Inside, the cartridge slot is not
used at all.

---

## 4. Why NOT through the cartridge slot (in this concept)

The cartridge slot is **not a general-purpose peripheral bus** — it's a closed,
authenticated interface for exactly one thing: talking to a genuine game card. It's
closed on three sides at once:

- **Data.** Rumble physically doesn't go through the slot. The game sends it to hid,
  not to the gamecard bus. Catching vibration "at the cartridge" means catching
  something that isn't there.
- **Authorization.** A non-original device triggers an authenticity error.
- **Power.** Current in the slot is gated by the same check — the console de-powers an
  unauthenticated device.

In this external concept, the slot isn't needed as a data channel or a power source —
both come from elsewhere (see section 3).

---

## 5. Components

**Software**
- An Atmosphere-based sysmodule, written with libnx.
- A rumble interception point in hid; forwarding values to the external channel.

**Hardware**
- An ESP32-class microcontroller (cheap, available, USB + BT out of the box).
- Actuator: the iPhone 12 Taptic Engine — a good size/quality compromise, fits a
  cartridge shell with a small upward extension.
- Power: USB-C passthrough or a small own battery. **Not the slot.**
- Enclosure: keep the cartridge form factor for aesthetics, but functionally it's an
  external dongle.

---

## 6. What to reverse (open questions)

1. **Where exactly in hid** to tap the active player's vibration values.
2. **Does the Lite populate** the built-in handheld controller's vibration device, or
   must a virtual controller be registered for the game to send it rumble.
3. **Channel latency** (USB vs BT) — so the vibration doesn't lag events.
4. **HD-rumble mapping** (amplitude/frequency) onto the chosen actuator for a decent
   feel.

All of this is found via the **switchbrew wiki + libnx/Atmosphere sources + hardware
experiments**, not a disassembler of the proprietary bus. A feasible first RE project.

---

## 7. Step plan

1. **Quick hypothesis check.** Connect a normal Joy-Con to the Lite and confirm games
   really send it rumble on the Lite. Confirms the signal is generated.
2. **Minimal sysmodule.** Catch vibration values in hid and just log them / light an
   LED on the external device on a button press. The first win.
3. **Forwarding.** Send the values over USB/BT to an ESP32, spin a test motor.
4. **Real actuator + mapping.** Connect the Taptic engine, tune the HD-rumble mapping.
5. **Enclosure and polish.** Fit the form factor, tune latency and power draw.

---

## 8. Project assessment

Doable — especially via the sysmodule path. Medium difficulty: not trivial, but not a
"star problem." The main risk isn't "code brilliance" but the **"edit → test on a live
console → analyze" loop** and reversing the interception point. Normal engineering, not
a dream.

The slot architecture is pretty on paper but solves the wrong problem in this concept,
so it was set aside in favour of the simpler path.

---

## 9. The main goal

> Give the Nintendo Switch Lite working tactile feedback through a plug-and-play
> external actuator driven by a custom sysmodule — catch the live rumble signal in hid
> and carry it to hardware that can vibrate.
