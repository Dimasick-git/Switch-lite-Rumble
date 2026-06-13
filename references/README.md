# references/

Upstream projects we study and reuse, vendored as **git submodules** (pinned
pointers — they don't bloat this repo's history). Fetch them on demand:

```sh
git submodule update --init references/<name>      # one
git submodule update --init --recursive            # all (incl. libstratosphere)
```

Each submodule keeps its **own license** — see the `LICENSE` file inside it. They are
referenced here for study and reuse under their respective terms; nothing from them
is copied into this repo's own source.

## Vendored here

| Path | Upstream | Why it's here |
| :--- | :--- | :--- |
| [`MissionControl/`](./MissionControl) | [Dimasick-git/Mission-Control](https://github.com/Dimasick-git/Mission-Control) (our fork of [ndeadly/MissionControl](https://github.com/ndeadly/MissionControl), tracks upstream daily) | **HD-rumble decode.** Converts the `HidVibrationValue` bands into a drive signal for third-party actuators — directly reusable for our actuator-mapping milestone. Also a mature libstratosphere MITM (btdrv) reference. |
| [`Nintendo_Switch_Reverse_Engineering/`](./Nintendo_Switch_Reverse_Engineering) | [dekuNukem/…](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) | The canonical **HD-rumble byte-format and frequency/amplitude lookup tables** (`rumble_data_table.md`) behind [`../docs/RUMBLE-ENCODING.md`](../docs/RUMBLE-ENCODING.md). |
| [`sys-con/`](./sys-con) | [cathery/sys-con](https://github.com/cathery/sys-con) | libstratosphere controller sysmodule; documents the **virtual-controller vibration limit** that validates our MITM design (see [`../RESEARCH.md`](../RESEARCH.md) §13). |
| [`switch-examples/`](./switch-examples) | [switchbrew/switch-examples](https://github.com/switchbrew/switch-examples) | Minimal, correct **libnx vibration example** (`hid/vibration`). |
| [`nxdumptool/`](./nxdumptool) | [DarkMatterCore/nxdumptool](https://github.com/DarkMatterCore/nxdumptool) | The clearest **cartridge ↔ FS** reference: gamecard handle/CardId/Certificate/InitialData via the legit FS API, and LAFW dump from RAM. Maps the FS layer the slot-power idea must work with. |

> Build dependency (separate, under `software/`): **libstratosphere**
> ([Atmosphere-libs](https://github.com/Atmosphere-NX/Atmosphere-libs)) is vendored
> at [`../software/rumble-logger-mitm/libstratosphere`](../software/rumble-logger-mitm/libstratosphere)
> because the MITM module links against it.

## Referenced by link only (intentionally not vendored)

- **[4IFIR](https://github.com/rashevskyv/4IFIR)** (Cooler3D's voltage work) — the
  source of [`../software/gc-power/`](../software/gc-power/). **Not** vendored: it
  contains the author's unrelated/private work (incl. audio-EQ code) that is out of
  scope for this repo and must not be redistributed here. Only the game-card power
  domains were reimplemented, with credit.
- [Switch-OC-Suite](https://github.com/KymPossibl/Switch-OC-Suite),
  [Linux MAX77620 driver](https://github.com/torvalds/linux/blob/master/drivers/regulator/max77620-regulator.c)
  — PMIC register references.
- [BetterJoy](https://github.com/Ryochan7/BetterJoy),
  [JoyShockLibrary+HDRumble](https://github.com/MIZUSHIKI/JoyShockLibrary-plus-HDRumble)
  — PC-side HD-rumble encode/decode cross-references.
- [libnx](https://github.com/switchbrew/libnx) — provided by the devkitPro toolchain,
  not vendored (see [`../software/DEPENDENCIES.md`](../software/DEPENDENCIES.md)).

Full annotated list with context: [`../RESEARCH.md`](../RESEARCH.md) §14.
