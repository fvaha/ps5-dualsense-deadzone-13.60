PS5_HOST ?= ps5
PS5_PORT ?= 9021
ELF = ps5-dualsense-deadzone.elf

all:
	python3 -B build.py

test: BUILD = $(shell ls -td build/payload-* | head -1)
test:
	clang -arch x86_64 -std=gnu11 -O2 -Wall -Wextra -Werror -I src -I "$(BUILD)" tests/test_dual.c -o "$(BUILD)/test_dual"
	"$(BUILD)/test_dual"
	node tests/check_ui.js
	python3 -u tests/test_runtime.py . "$(BUILD)"
	python3 -u tests/test_followup.py .
	python3 -u tests/test_persistence.py .
	node tests/check_ui_runtime.js .

deploy: $(ELF)
	curl -m 30 --data-binary @$(ELF) http://$(PS5_HOST):$(PS5_PORT)/

.PHONY: all test deploy
