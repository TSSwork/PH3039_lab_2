# PH3039 Lab 2: "on"/"off" keyword spotting with Edge Impulse on a Seeed XIAO

This lab is the Edge Impulse exercise from
`reports/docs/EA 3120 - Edge Impulse - Nano 33 BLE.pdf`, adapted for the
**Seeed Studio XIAO nRF52840 Sense** in place of the Arduino Nano 33 BLE Sense.

The handout does everything inside the Edge Impulse platform. This repo moves
most of the work onto the PC:

| Step | Where |
|------|-------|
| Record audio from the XIAO's PDM microphone and save it as WAV | Local (`src/main.cpp` and `scripts/ei_capture.py`) |
| Upload the WAVs, design the impulse (MFCC + classifier), train it | Edge Impulse Studio |
| Export the model as an Arduino library (`.zip`) | Edge Impulse Studio |
| Build, flash and run the model on the board, and log its predictions | Local (`src/edge_model.cpp`, arduino-cli, CMake/Ninja) |

When the model hears "on" it switches an external LED on (pin D2). When it
hears "off" it switches the LED off. It ignores silence and fan noise.

---

## Repository layout

```
src/main.cpp          Firmware 1: mic capture. Streams 2 s, 16 kHz PCM clips on request.
src/edge_model.cpp    Firmware 2: runs the Edge Impulse classifier and drives the LED.
src/fmt.h             csvln()/logln() serial helpers.
scripts/ei_capture.py PC side of capture: asks the board for a clip and writes a .wav.
scripts/log.sh        Logs serial output to data/log_<stamp>.{csv,log}.
edge_data/<label>/    Recorded training clips (on, off, noise_none, noise_fan).
ei-tsswork-project-1-arduino-1.0.1-on-off.zip   Model v1 (ON/OFF only, superseded).
ei-tsswork-project-1-arduino-1.0.2-on-off.zip   Model v2 (current, with noise classes).
project.yaml          Board, port, CSV header, libraries.
boards/xiao.env       FQBN and baud rate for the XIAO.
Makefile, CMakeLists.txt  Build system (see "How the build picks a firmware").
reports/docs/         Lab handout and the XIAO datasheet.
```

---

## Prerequisites

- **arduino-cli**, **cmake**, **ninja**, **python3**
- **uv**, used to run `scripts/ei_capture.py`. It installs `pyserial` on its own.
- The Seeed nRF52 core. This repo was built with `Seeeduino:nrf52` 1.1.13:
  ```sh
  arduino-cli config add board_manager.additional_urls \
      https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
  arduino-cli core update-index
  arduino-cli core install Seeeduino:nrf52
  ```
- The PDM library:
  ```sh
  arduino-cli lib install "nRF52840 PDM - Adafruit Fork"
  ```
- The Edge Impulse model library, installed from the zip in this repo. Use v2:
  ```sh
  arduino-cli config set library.enable_unsafe_install true
  arduino-cli lib install --zip-path ei-tsswork-project-1-arduino-1.0.2-on-off.zip
  ```
  The library installs as `TSSwork-project-1_inferencing` under
  `~/Arduino/libraries/`. Installing another zip replaces it.

> The CMake build does **not** install libraries automatically. The `libs:`
> list in `project.yaml` only documents what is needed, so run the install
> commands above yourself.

Board settings are in `project.yaml` (`board: xiao`, `port: /dev/ttyACM0`). If
your port is different, edit the file or add `PORT=...` to any make command.

---

## How the build picks a firmware

Both `src/main.cpp` and `src/edge_model.cpp` define `setup()`/`loop()`, so
only one of them can go into a build. CMake copies the chosen file
(`SKETCH_ENTRY`) and the headers in `src/*.h` into `build/<board>/sketch/sketch.ino`
and compiles only that.

| Command | Firmware |
|---------|----------|
| `make build` / `make upload` / `make run` | `main.cpp` (capture) |
| `make build_model` / `make upload_model` / `make run_model` | `edge_model.cpp` (inference) |

Other targets: `make monitor` (raw serial monitor), `make log`, `make clean`,
`make help`.

---

## Workflow

### 1. Record training data (local)

Flash the capture firmware:

```sh
make upload
```

`main.cpp` reads the PDM mic at 16 kHz. By default it prints a `time_ms,state,rms`
loudness row for each audio chunk. When it receives `R` over serial, it sends one
2-second clip (32,000 samples) in a small binary format (`PCMW` header, then
checksummed packets). `scripts/ei_capture.py` reads that format and writes a mono
16-bit WAV.

Record clips for each label. Close any serial monitor first:

```sh
make wav LABEL=on         COUNT=20
make wav LABEL=off        COUNT=20
make wav LABEL=noise_none COUNT=30   # a quiet room
make wav LABEL=noise_fan  COUNT=30   # a fan running
```

For each clip, press Enter, then say the word. Files go to
`edge_data/<label>/<label>_NNN_<timestamp>.wav`. The script prints the peak and
RMS of each clip and warns you when a clip is too quiet or clipping. You can
tune this with `PDM.setGain()`.

### 2. Train in Edge Impulse Studio

1. **Data acquisition → Upload data**: upload each `edge_data/<label>/` folder
   with its label (the model uses `ON`, `OFF`, `NOISE_NONE`, `NOISE_FAN`).
   Let Studio split the data into training and test sets.
2. **Impulse design**: Time series input, 16000 Hz, **600 ms window** (9600
   samples). Add an **MFCC** processing block and a **Classification** learning
   block.
3. Generate features, train, and check the confusion matrix and the test results.
4. **Deployment → Arduino library**, quantized (int8). Download the zip into
   the repo root.

### 3. Run the model on the board (local)

Install the zip you just exported (see Prerequisites), then:

```sh
make run_model CSV_HEADER=time_ms,label,score
```

This builds `edge_model.cpp`, flashes it, and logs serial output to
`data/log_<stamp>.csv`/`.log`. Stop with Ctrl+C. Use `make upload_model` followed
by `make monitor` if you only want to watch the output.

> Pass `CSV_HEADER=time_ms,label,score` because `project.yaml` holds the capture
> firmware's header (`time_ms,state,rms`).

What `edge_model.cpp` does:

- The PDM callback fills a buffer of `EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE`
  samples (9600 = 600 ms).
- When the buffer is full, it runs `run_classifier()` on the window, prints
  `time_ms,label,score` for the top class, and empties the buffer.
- If the top score is above **0.60**: `on` sets D2 HIGH, `off` sets D2 LOW, and
  any noise class is logged and ignored. The label check ignores case.
- It prints a `# Board alive...` heartbeat every 2 s.

Wiring: connect an LED and a resistor (about 220–330 Ω) from **D2** to GND.

---

## Model history

| Version | Zip | Classes | Data | Result |
|---------|-----|---------|------|--------|
| v1 | `...-1.0.1-on-off.zip` | ON, OFF | 20 + 20 clips | Worked for the words, but silence and background noise also got classified as ON or OFF, so the LED toggled at random. |
| v2 (current) | `...-1.0.2-on-off.zip` | ON, OFF, NOISE_NONE, NOISE_FAN | plus 30 silence + 30 fan clips | Noise now has its own classes and is ignored. The LED only reacts to the spoken words. |

Both versions take 16 kHz audio in a 600 ms window, use MFCC features, and run
an int8 TFLite model.

To add more classes or more data, record with `make wav LABEL=...`, retrain,
export, install the new zip, and run `make run_model`. If the new model has
labels other than `on`/`off`, update the LED logic in `edge_model.cpp`.

---

## Troubleshooting

- **Capture times out**: another program has the serial port open (a monitor
  or a logger). Close it and try again.
- **`TSSwork-project-1_inferencing.h: No such file`**: the model library is not
  installed. See Prerequisites.
- **Upload fails or the port is not found**: double-tap the XIAO's reset button
  to enter the bootloader, then check `ls /dev/ttyACM*` and pass `PORT=...`.
- **LED reacts to background sound**: record more noise clips in that
  environment as a new class, or raise the 0.60 threshold.
- **Too many false triggers or missed words**: the model classifies separate,
  back-to-back 600 ms windows with no overlap, so a word that falls across two
  windows can be missed. Say the word clearly, then pause.

## Serial convention

`csvln(...)` prints data rows, and `logln(...)` prints messages that start with
`# `. `scripts/log.sh` keeps the `# ` lines out of the CSV (they still go to the
`.log` file).
