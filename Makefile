SC_REASM ?= $(HOME)/dev/sc-reasm
.PHONY: all tools check check-arm check-slice inventory
all: tools

tools:
	./tools/build-tools.sh

check: check-arm
	bash tests/slice/runner.sh

check-arm: tools
	./tools/test-freestanding.sh

check-slice: tools
	bash tests/slice/runner.sh

inventory:
	@mkdir -p build
	python3 tools/inventory.py "$(SC_REASM)/bin/StarCraft.exe" "$(SC_REASM)/analysis/ida_names.json" > build/inventory-exe.json
	python3 tools/inventory.py "$(SC_REASM)/storm/storm.dll.orig" "$(SC_REASM)/storm/analysis/ida_names_full.json" > build/inventory-storm.json
