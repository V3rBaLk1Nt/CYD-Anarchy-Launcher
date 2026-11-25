# CYD Anarchy Launcher

A custom touch launcher and app ecosystem for the **Cheap Yellow Display** (CYD 2.4" ESP32-2432S028).

## Status

Active development. Working firmware, touchscreen support, and base apps are in this repository. Visual redesign and additional apps are under construction.

## Hardware

| Item | Detail |
|------|--------|
| Board | ESP32-2432S028 (Cheap Yellow Display) |
| Display | 2.4" 240×320 resistive touch |
| Storage | microSD + flash |
| Variants | Elecrow 2.4B and compatible CYD boards |

## What's here

- **Firmware** — prebuilt `.bin` images in repo root and `firmware/`
- **Launcher source** — `src/` (display, touch, apps, WiFi/BT tools, settings)
- **Apps** — modular app registry under `src/` and `apps/`
- **Boards** — PlatformIO board configs in `boards/`
- **Web UI** — `webUi/` for on-device configuration
- **Native host build** — `native/` for desktop testing

## Planned

- Touch-first home screen and app grid redesign
- Modular third-party app loading
- Theme support
- Expanded red-team / blue-team tooling (not implemented yet)

## Build

Requires [PlatformIO](https://platformio.org/). See `platformio.ini` and `docs/BUILD.md` if present.

```sh
pio run -e <environment>
```

## Attribution

Based on prior CYD/M5Stack launcher work. Third-party library licenses are preserved in their respective directories under `lib/`.

No project license has been assigned yet — all rights reserved by the author until one is chosen.
