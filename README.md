# Tap Timer

[Deutsch](README.de.md)

Tap Timer is a tactile countdown timer for Pebble smartwatches. On
touch-capable models, the duration is selected directly on a vertical minute
ruler instead of through menus. Every minute is a stable detent: crossing the
midpoint rolls the ruler cleanly to the neighbouring minute.

After the ruler is released, the selector settles, the chevrons fill, and the
countdown starts automatically.

## Highlights

- Touch-driven vertical ruler from 0 to 180 minutes
- Precise one-minute detents with a smooth mechanical rolling motion
- Fixed read line with an animated bubble around five-minute labels
- Automatic start sequence with animated chevrons
- Custom high-contrast PPF pixel digits
- Calm `MM:SS` countdown by default
- Optional hundredths of a second below 100 minutes
- Light, Dark and wrist-shake theme modes
- Independently configurable read-line colour
- Configurable alarm vibration and optional speaker beep
- Persistent running and paused timer state

## Controls

### Selecting a duration

Drag the ruler on a touch-capable Pebble. The ruler always rests exactly on a
minute mark. Release it to start the automatic launch animation.

Hardware buttons remain available, and classic Pebble models use the
button-based interface.

### While the timer is running

- **Top left:** minimise the timer
- **Middle right:** pause or resume
- **Top right while paused:** add one minute
- **Bottom right while paused:** delete the timer

## Settings

The phone configuration page contains only settings that affect the current
interface:

- Alarm vibration pattern
- Beep volume on speaker-capable models
- Light, Dark or Shake appearance
- Optional hundredths of a second
- Read-line colour

Hundredths are disabled by default for a calmer display. Even when enabled,
they are hidden at 100 minutes and above so the custom digits remain readable.

## Compatibility

The package targets the supported Pebble SDK 3 platforms. The vertical ruler
requires a touch-capable device; non-touch models retain button operation.

## Building

Requirements:

- Pebble SDK and CLI
- Node.js and npm

Install the Clay dependency and build:

```bash
npm install
pebble clean
pebble build
```

Install on a watch reachable through the Pebble developer connection:

```bash
pebble install --phone <watch-ip>
```

## Origin and licence

Tap Timer is based on **Instant Timer** by Andrew Howe and was extensively
redesigned by Maru Kitano.

This project is free software licensed under the
[GNU General Public License version 3](LICENSE).
