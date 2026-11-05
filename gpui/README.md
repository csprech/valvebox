# Valvebox with a GPUI front end (experimental)

This branch (`gpui`) draws and plays Valvebox in a [GPUI](https://gpui.rs) window. `main` keeps the JUCE app as it was.

How it's put together:
- **The instrument is unchanged.** The C++ engine and the JUCE panel sit behind a C interface (`bridge/PanelBridge.h`). The panel runs off-screen and stays the controller, so every key, knob, screen, menu and dialog behaves as in the app.
- **The panel draws its own parts.** `bridge/ValveboxAdapter.cpp` lists them and gives each a cheap signature (a key's light, a knob's value, the display's text) that changes whenever the part would look different.
- **GPUI does the rest.** It lays the panel out at any window size and composites the parts on the GPU, redrawing a part only when its signature changes. It sends the mouse (clicks, drags, the scroll wheel) and the computer keyboard back to the panel in panel coordinates.
- **The panel has a stand-in window.** The bridge gives it a JUCE peer that never draws, kept where the panel is on screen. JUCE then delivers the mouse the normal way, as if the panel were in a window of its own, with hover and double-clicks. That includes fields whose text hands clicks on to them, such as menus on a screen. Menus and pop-ups open over the panel, at its size.
- **Tooltips:** rest the mouse on a part. It shows the part's own tooltip, its name or caption, or a slider's or menu's value.
- **Pictures GPUI no longer shows are let go of.** GPUI keeps every picture it's given in its GPU atlas until it's told to drop it, so each replaced part picture is dropped two frames later.

## Building

You need Rust (rustup), CMake, Ninja and the Xcode Command Line Tools. Full Xcode isn't needed: GPUI's shaders compile at runtime.

```sh
scripts/bootstrap.sh            # JUCE into external/JUCE
cd gpui
cargo run --release
```

`build.rs` builds the C++ side (`valvebox_bridge`, a static library with JUCE) with CMake and links it.

## Self-test

Every key is pressed, and every knob, dial and slider dragged, through the window's own mouse handling, each checked to change on the panel. Then it checks that the pictures redrawn are dropped from the GPU atlas, and the device plays and is checked for sound.

To run it:

```sh
VALVEBOX_GPUI_SELFTEST=/tmp/report.txt cargo run --release
```
