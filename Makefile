.DEFAULT_GOAL := help

PY := $(shell command -v python3 || command -v python || command -v py)

# project.yaml -> build/config.mk (regenerated automatically when it changes).
# Values use `?=`, so command-line / environment settings still override the file.
build/config.mk: project.yaml scripts/config.py
	@mkdir -p build
	@$(PY) scripts/config.py > $@
-include build/config.mk

BOARD ?= uno
BOARDFILE := boards/$(BOARD).env

ifeq ($(wildcard $(BOARDFILE)),)
$(error Board file $(BOARDFILE) not found. Set board: in project.yaml or use BOARD=uno, xiao, ...)
endif

include $(BOARDFILE)

# Lazy: only evaluated by targets that use $(PORT), so `make build` needs no board.
PORT ?= $(shell $(PY) scripts/port.py)
BAUD ?= 9600
CSV_HEADER ?=
LIBS ?=

LABEL ?= unknown
COUNT ?= 1
WAV_DIR ?= edge_data
UV ?= uv

BUILD := build/$(BOARD)
NINJA := ninja -C $(BUILD)

.PHONY: help configure build upload monitor log run clangd clean wav

help:
	@echo "daq-template: build/upload/log firmware with arduino-cli (CMake + Ninja)"
	@echo ""
	@echo "Quick start: edit project.yaml, then run:  make run"
	@echo ""
	@echo "Usage: make <target> [BOARD=<name>] [PORT=<port>] [CSV_HEADER=\"a,b,c\"]"
	@echo ""
	@echo "Targets:"
	@echo "  run        build + upload + log in one go; stops at the first failure"
	@echo "  build      Stage src/ as a sketch, install libs, compile"
	@echo "  upload     Build, then flash the board"
	@echo "  monitor    Open the serial monitor"
	@echo "  log        Save serial output to data/log_<stamp>.csv and .log"
	@echo "  clangd     Generate .clangd for the editor (installs libs, run after a good build)"
	@echo "  clean      Remove build/"
	@echo ""
	@echo "project.yaml keys (command-line values override them):"
	@echo "  board       Profile in boards/<name>.env. Available: $(basename $(notdir $(wildcard boards/*.env)))"
	@echo "  csv_header  Header line the firmware prints, e.g. time_ms,temp_c,adc"
	@echo "  port        Optional; auto-detected if unset (e.g. /dev/ttyACM0, COM3)"
	@echo "  libs        Arduino libraries to auto-install, e.g. [Adafruit BMP280 Library]"
	@echo ""
	@echo "Complete logging example (Uno):"
	@echo "  1. Firmware prints the header once, then one row per sample, in src/main.cpp:"
	@echo "       csvln(\"time_ms,temp_c,adc\");           // in setup()"
	@echo "       csvln(\"{},{},{}\", millis(), t, adc);  // in loop()"
	@echo "  2. project.yaml:"
	@echo "       board: uno"
	@echo "       csv_header: time_ms,temp_c,adc"
	@echo "  3. make run          (add PORT=/dev/ttyACM0 if auto-detect fails; Ctrl+C to stop)"
	@echo "  4. Output: data/log_<YYYYMMDD_HHMMSS>.csv (header + rows) and .log (everything)"
	@echo ""
	@echo "Serial convention: csvln() = data rows, logln() = '# ' messages kept out of the CSV."
	@echo "Add a board: create boards/<name>.env with FQBN= and BAUD=."

configure:
	cmake -S . -B $(BUILD) -G Ninja -DFQBN="$(FQBN)" -DLIBS='$(LIBS)'

build: configure
	$(NINJA) build

upload: configure
	PORT="$(PORT)" $(NINJA) upload

monitor:
	arduino-cli monitor --port "$(PORT)" --config baudrate="$(BAUD)"

log:
	@test -n "$(CSV_HEADER)" || { echo "csv_header is empty: set it in project.yaml (or CSV_HEADER=...)" >&2; exit 1; }
	./scripts/log.sh "$(PORT)" "$(BAUD)" "$(CSV_HEADER)"

# Sub-makes re-detect the port, since the board can re-enumerate after flashing.
run:
	$(MAKE) upload
	@sleep 2
	$(MAKE) log

wav:
	@test -n "$(PORT)" || { echo "PORT is empty: set PORT=/dev/ttyACM0" >&2; exit 1; }
	$(UV) run scripts/ei_capture.py --port "$(PORT)" --label "$(LABEL)" --count "$(COUNT)" --outdir "$(WAV_DIR)"

clangd: configure
	$(NINJA) libs
	./scripts/setup-clangd.sh "$(BOARD)"

clean:
	rm -rf build


# Add 'build_model', 'upload_model', 'run_model' to your .PHONY list
.PHONY: help configure build upload monitor log run clangd clean build_model upload_model run_model

# ... (keep all your existing Makefile rules exactly as they are) ...

# ==============================================================================
# Edge Impulse Model Targets
# ==============================================================================
MODEL_ENTRY := edge_model.cpp

build_model:
	@echo "Building Edge Impulse model ($(MODEL_ENTRY))..."
	$(MAKE) configure SKETCH_ENTRY=$(MODEL_ENTRY)
	$(NINJA) -C $(BUILD) build

upload_model: build_model
	@echo "Uploading Edge Impulse model to $(PORT)..."
	PORT="$(PORT)" $(NINJA) -C $(BUILD) upload

run_model: upload_model
	@echo "Starting serial logging for Edge Impulse model..."
	@sleep 2
	$(MAKE) log
