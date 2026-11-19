# SickoLooper3 controller

In PLAY, hold **SHIFT**, tap **←**, then release. The pad switches to SickoLooper3. **SHIFT + →** returns to PLAY. The groovebox sequencer keeps playing when you change modes. Its transport buttons still operate the sequencer. Use the bottom row of pads for Rack’s transport.

## Install the matching builds

On Linux x64, close Rack, place the modified SickoCV `.vcvplugin` package in `~/.local/share/Rack2/plugins-lin-x64/`, and reopen Rack. It uses the existing SickoCV module slug so patches with sickoLooper3 open with the new controls. A later VCV Library update of SickoCV can replace this custom build.

The SMC-PAD needs the matching GrooveBox firmware and the open firmware bootloader. Enter its bootloader with **SHIFT + PLAY** at power-on, then upload `GrooveBox-companion.bin` with `dfu-util -d 1209:0002 -a 0 -D GrooveBox-companion.bin`. The updated desktop companion runs from the GrooveBox checkout with `tools/run_companion.sh`.

## Connect Rack

Use the GrooveBox firmware and the matching [SickoCV fork](https://github.com/smerkley/SickoCV-mod). In Rack, right-click **sickoLooper3** and find **GrooveBox pad · MIDI channel 16**. Under **Commands from pad**, select the pad’s MIDI input. Under **Feedback to pad**, select its MIDI output. Use the **Driver** submenu if the device is on a different MIDI driver. Rack saves the selection with the patch. Connect just one looper instance to a given pad.

Start with USB. Select **SMC-PAD open fw** or the corresponding OS MIDI port. For Bluetooth, the computer running Rack needs the BLE MIDI connection. The pad supports one Bluetooth host at a time. The companion can also read the pad’s USB serial connection while Rack uses USB MIDI.

Reserve **MIDI channel 16** on this connection. Keep sequencer tracks on other channels and set instrument MIDI inputs to their musical channels. The factory drum kit uses channel 10. Looper commands use ordinary three-byte MIDI messages.

## Play the looper

Rows are listed from the top of the physical pad. In each loop row, the four pads read left to right. The paired knobs on that row control the same loop.

| Row | Pads | Knobs |
|---|---|---|
| Loop 1 | Record/overdub · Play · Stop · Solo | Volume · Pan |
| Loop 2 | Record/overdub · Play · Stop · Solo | Volume · Pan |
| Loop 3 | Record/overdub · Play · Stop · Solo | Volume · Pan |
| Global | Start/stop all · Stop all · Click · Pre-roll | Master volume · Tempo |

For a first recording, patch audio to source 1, turn off its source mute if lit, then tap Record in the top row. With loop sync enabled, the module schedules the transition on its clock.

Green indicates playback, red indicates recording, and overdub alternates red and amber on the record pad. Amber on the stop pad means a recorded loop is idle. A blinking action means a transition is pending. The mode reads these states from the module, so the lights also follow actions taken in Rack. Quantization and the behavior of Record and Play follow the module’s settings. Change Solo while the loop is empty or stopped. Reverse takes effect at the loop boundary.

| Layer | Pads in each loop row | Global pads |
|---|---|---|
| Hold SHIFT | Reverse · One-shot · Loop sync · Erase | External clock bar reset · Click to master · Pre-roll length (1 or 2 bars) · Request feedback |
| Toggle PAD BANK | Start immediately · Stop immediately · Monitor source to track output · Mute source | Same transport and click controls as the main bank |
| Toggle KNOB BANK | Knobs adjust loop measures and crossfade | Knobs adjust ear volume and click volume |

SHIFT takes priority over the pad bank. To erase, stop the loop, then double-tap its **SHIFT + Erase** pad. Source mute affects the corresponding module input. The two bank buttons work independently, so you can keep the performance pads visible while editing loop lengths. Use the companion to read the assignments and module feedback for the current layer.

## MIDI reference

All addresses below are decimal. Loop indices run from 0 through 2 in top-to-bottom order. Column indices run from 0 through 3. Positive-velocity Note On triggers a command. Note Off and zero-velocity Note On are ignored.

| Pad → Rack | Meaning |
|---|---|
| Notes 36–47 | Loop command at `36 + 4 × loop + column` in the main layer |
| Notes 48–51 | Main global row |
| Notes 52–67 | SHIFT layer in the same row order. Note 67 requests a full feedback snapshot. |
| Notes 68–79 | Banked loop commands. The pad sends notes 48–51 for the banked global row. |
| CC 20–27 | Relative knob changes in hardware order, starting at the bottom: Master, Tempo, Loop 3 volume/pan, Loop 2 volume/pan, Loop 1 volume/pan |
| CC 28–35 | Ear volume, Click volume, then loop measures/crossfade for loops 3, 2, 1 |

Encoder values use signed, seven-bit two’s complement. A value of 1 increments and 127 decrements. The module applies the change to the current parameter value.

| Rack → pad | Meaning |
|---|---|
| CC 40–42 | Current state for loops 1–3: 0 empty, 1 stopped, 2 pre-roll, 3 playing, 4 recording, 5 overdubbing |
| CC 43–45 | Pending action for loops 1–3: 0 none, 1 stop, 3 play, 4 record, 5 overdub |
| CC 46–48 | Loop flags, bits 0–6: solo, reverse, one-shot, loop sync, immediate start, immediate stop, source monitoring |
| CC 49 | Global flags, bits 0–5: transport active, click on, pre-roll enabled, click to master, two-bar pre-roll, pre-roll in progress |
| CC 50 | Source mute flags for inputs 1–3 in bits 0–2 |
| CC 51 | Heartbeat, value 1 |
| CC 52–67 | Absolute values (0–127) for the 16 knobs listed above, main bank first. BPM and crossfade readings in the companion are approximate. |

The module reports changed values and provides a snapshot on request. The pad blinks queued actions locally. Audio and MIDI clock do not depend on companion display updates.

## Linux build and regression test

From the fork’s root, set `RACK_DIR` to the extracted Rack SDK and `RACK_RUNTIME` to the matching Rack installation directory containing `libRack.so`. The regression runs the actual module with an in-memory MIDI port and an audio input. It exercises recording, playback, overdubbing, erase protection, feedback, pre-roll, and independent loop commands after Stop All.

```sh
make -j4 RACK_DIR="$RACK_DIR" dist
c++ -std=c++17 -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" \
    tests/groovebox.cpp -L"$RACK_RUNTIME" -Wl,-rpath,"$RACK_RUNTIME" \
    -lRack -ldl -o /tmp/groovebox-test
/tmp/groovebox-test "$PWD"
```
