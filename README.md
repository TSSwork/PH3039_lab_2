# daq-template

arduino-cli + CMake/Ninja template for microcontroller data acquisition.
Write firmware in `src/main.cpp`; it is staged as a sketch automatically.

1. Edit `project.yaml` (board, csv_header, optional port, Arduino libs).
2. `make run` builds, uploads, then logs; it stops at the first failure.

Run `make help` for all targets and a complete example.
Other targets: `build`, `upload`, `monitor`, `log`, `clangd`, `clean`.
Command-line values override the file: `make run BOARD=xiao PORT=/dev/ttyACM0`.

Serial convention (see `src/fmt.h`): `csvln(...)` prints data rows, `logln(...)`
prints `# `-prefixed messages that stay out of the CSV. The firmware only prints;
`scripts/log.sh` on the PC writes `data/log_<stamp>.{csv,log}`.

`csv_header` must match the header line the firmware prints.
Libraries listed under `libs:` are installed with `arduino-cli lib install`.
Add a board: create `boards/<name>.env` with `FQBN=` and `BAUD=`.
