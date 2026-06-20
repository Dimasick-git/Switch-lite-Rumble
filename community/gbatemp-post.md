# Draft GBAtemp post

> Ready to copy-paste into a new GBAtemp thread. Framed around the rumble /
> accessibility goal (capturing the vibration the console already computes and
> driving an actuator) — **not** around defeating gamecard authentication, since
> that framing falls under GBAtemp's flashcart/piracy rules and is the likely reason
> the previous thread was removed. Adjust the tone as you like before posting.

---

**Title:** Switch Lite Rumble — open homebrew project (capture HD-rumble from `hid` → drive a real actuator)

**Suggested forum:** Nintendo Switch → Switch - Homebrew Development & Emulators

---

Hi all,

The **Switch Lite has no rumble** — there's no motor in the body. But the firmware
side isn't gone: the Lite works with paired Joy-Con / Pro Controllers that *do*
vibrate, so the `hid` subsystem still **computes vibration values** for every game.
The signal is alive; it just has no actuator to play on.

This project's goal: **capture that live vibration and feed it to a small actuator**
so the Lite itself buzzes — an accessibility/feel upgrade, fully homebrew, no piracy
angle.

**What already works (open source, builds in CI):**

- A custom **Atmosphère sysmodule** that MITMs the `hid` service and logs the
  vibration a game emits — `SendVibrationValue` (201) and `SendVibrationValues` (206)
  — then forwards every call untouched, so games behave normally. It compiles green in
  CI and ships an SD-ready package.
- **Haptics research**: mapping the two-band HD-rumble (`HidVibrationValue`) onto a
  single resonant actuator. Target actuator is an **iPhone-12 Taptic Engine** driven
  by a **TI DRV2605L**; the decode math is reusable from MissionControl.
- **Power notes**: actuator peak (>300 mA) needs a small supercap/Li-Po buffer.

**Open questions / where help is wanted:**

- Anyone who's looked at **hid vibration capture** on a Lite — does the *handheld*
  npad actually receive vibration values, or do we need to register a virtual
  controller?
- **Atmosphère / sysmodule devs** — to harden the capture + add forwarding (USB-C/BT).
- **Hardware / PCB folks** — fitting a Taptic Engine + driver into a small module
  (in-cartridge shell or an external dongle), with left/right actuators for "full"
  rumble.
- **Testers** with CFW.

Repo (code, research, design docs, references):
**https://github.com/Dimasick-git/Switch-lite-Rumble**

Everything's documented and the sysmodule is downloadable from the repo's Actions
artifacts. Feedback, ideas, and contributors welcome — drop a reply or open an issue.

Thanks!
