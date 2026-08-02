# Tap Timer

Tap Timer is a touch-focused timer app for Pebble smartwatches, developed by
[marukitano](https://github.com/marukitano).

Instead of entering a duration through menus, the timer can be set directly on
the watch using a vertical minute ruler. Drag the ruler to select a duration,
release it, and the animated chevrons return to the centre and fill in before
the countdown starts automatically.

## Features

- Touch-based vertical minute ruler
- Timer durations from 1 to 180 minutes
- Animated chevrons with a spring-like return movement
- Automatic start animation after selecting a duration
- Minimal, high-contrast running timer screen
- Precise countdown display with centiseconds
- Configurable colours and themes
- Configurable vibration and alarm audio
- Traditional Pebble button controls remain available
- Support for multiple Pebble platforms

## Status

Tap Timer is currently under active development. Version 1.0.0 represents the
first independent release of the redesigned app.

Bug reports, suggestions and contributions are welcome.

## Building

The project uses the Pebble SDK.

```bash
pebble build
```

To install the generated package on a connected watch or emulator, use the
usual Pebble SDK installation command for your setup.

## Origin and licence

Tap Timer is based on **Instant Timer** by Andrew Howe and has been extensively
modified and redesigned by Maru Kitano.

The project is free software released under the
[GNU General Public License version 3](LICENSE).
