# Scotty demo harness

Runs the **real** Scotty QML UI against a fabricated, PII-free controller so each
screen can be screenshotted without the Nearby engine, a Google account, or your
real hostname. Nothing here ships in the app; it exists to produce marketing/README
screenshots.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

## Run a screen

```sh
SCOTTY_DEMO=home    ./build/scotty-demo   # idle: "Ready to receive" + blob
SCOTTY_DEMO=send    ./build/scotty-demo   # Nearby devices list
SCOTTY_DEMO=receive ./build/scotty-demo   # an incoming transfer in progress
```

Then screenshot the window (GNOME: `Print`/area-capture; or `gnome-screenshot -w`).

The fabricated data: device name `linux-pc`, a letter-only account avatar (no photo),
and the discovered devices `Pike's Laptop`, `Kirk's Pixel 10`, `Chromebook`
(a small Star Trek nod). Edit `demo_controller.h` to change any of it.
