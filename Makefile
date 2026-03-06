.ONESHELL:
.SHELLFLAGS := -euo pipefail -c

CLI := arduino-cli
CORE = arduino:avr
	TYPE = nano
	BOARD = $(CORE):$(TYPE)
	PORT ?= /dev/ttyUSB0
	NODETYPE ?= DISPLAY # NODE_SENSOR, NODE_MASTER

SRCDIR := ./src
BUILDDIR := ./build

# Fallback if the captive portal doesn't work
NODE_ID ?= NULL
HA_IP ?= NULL
MASTER_IP ?= NULL

ifneq ($(HA_IP), NULL)
	COMPILE_FLAGS += -DHA_IP_FIXED
endif
ifneq ($(MASTER_IP), NULL)
	COMPILE_FLAGS += -DMASTER_IP_FIXED
endif
COMPILE_FLAGS = -I./include -D$(NODETYPE)
ARDUINO_COMPILE_FLAGS = --build-property "compiler.cpp.extra_flags=$(COMPILE_FLAGS)" \
			--build-property "compiler.c.extra_flags=$(COMPILE_FLAGS)"

ARDUINO_DIR := $(HOME)/.arduino15
ARDUINO_CFG := $(ARDUINO_DIR)/arduino-cli.yaml


.PHONY: compile compile-master compile-display compile-sensor upload compiledb prerequisites clear

master:  NODETYPE = NODE_MASTER
master:  BOARD    = esp8266:esp8266:nodemcuv2
master:  BUILDDIR = ./build/master
master: COMPILE_FLAGS += -DHA_IP=$(HA_IP)
display: NODETYPE = DISPLAY
display: BOARD    = arduino:avr:nano
display: BUILDDIR = ./build/display
master display:
	$(CLI) compile . -b $(BOARD) --build-path $(BUILDDIR) $(ARDUINO_COMPILE_FLAGS)

sensor-%: NODETYPE = NODE_SENSOR
sensor-%: BOARD    = esp8266:esp8266:nodemcuv2
sensor-%: COMPILE_FLAGS += -DNODE_ID=$* -DHA_IP=$(HA_IP) -DMASTER_IP=$(MASTER_IP)
sensor-%: BUILDDIR = ./build/sensor-$*
sensor-%:
	$(CLI) compile . -b $(BOARD) --build-path $(BUILDDIR) $(ARDUINO_COMPILE_FLAGS)

upload-master: BOARD=esp8266:esp8266:nodemcuv2
upload-master: BUILDDIR=./build/master
upload-display: BOARD=arduino:avr:nano
upload-display: BUILDDIR=./build/display
upload-sensor-1: BOARD=esp8266:esp8266:nodemcuv2
upload-sensor-1: BUILDDIR=./build/sensor-1
upload-sensor-2: BOARD=esp8266:esp8266:nodemcuv2
upload-sensor-2: BUILDDIR=./build/sensor-2
upload-master upload-display upload-sensor-2 upload-sensor-1:
	$(CLI) upload . -p $(PORT) -b $(BOARD) -v --build-path $(BUILDDIR)

compiledb-master: NODETYPE=NODE_MASTER
compiledb-master: CORE=esp8266:esp8266
compiledb-master: BOARD=esp8266:esp8266:nodemcuv2
compiledb-sensor: COMPILE_FLAGS+= -DHA_IP=10 -DMASTER_IP=10
compiledb-display: NODETYPE=DISPLAY
compiledb-display: CORE=arduino:avr
compiledb-display: BOARD=arduino:avr:nano
compiledb-sensor: NODETYPE=NODE_SENSOR
compiledb-sensor: CORE=esp8266:esp8266
compiledb-sensor: BOARD=esp8266:esp8266:nodemcuv2
compiledb-sensor: COMPILE_FLAGS+= -DNODE_ID=1 -DHA_IP=10 -DMASTER_IP=10
compiledb compiledb-%: COMPILE_FLAGS+= -DCOMPILEDB
compiledb compiledb-sensor compiledb-master compiledb-display: clear prerequisites
	if [ -e $(CURDIR)/compile_commands.json ]; then \
		echo "compile_commands.json is already created"; \
		exit 0; \
		fi
	echo "Compiling..."
	$(CLI) compile . -b $(BOARD) \
		--only-compilation-database \
		--build-path $(BUILDDIR) \
		$(ARDUINO_COMPILE_FLAGS)
	if [ ! -e $(BUILDDIR)/compile_commands.json ]; then \
		echo "compile_commands.json was not generated"; \
		exit 0; \
		fi
	echo "Copying compile_commands.json"
	cp $(BUILDDIR)/compile_commands.json $(CURDIR)/compile_commands.json

prerequisites:
	mkdir -p $(ARDUINO_DIR)
	if grep -Fq 'arduino.esp8266.com/stable/package_esp8266com_index.json' "$(ARDUINO_CFG)" \
		&& [ "$(CORE)" = "esp8266:esp8266" ]; then \
		echo "esp8266 URL already present; skipping"; \
		exit 0; \
		fi
	echo "Modifying arduino-cli configs"
	printf '%s\n' \
		'board_manager:' \
		'  additional_urls:' \
		'    - https://arduino.esp8266.com/stable/package_esp8266com_index.json' \
		'network:' \
		'    connection_timeout: 6000s' \
		> $(ARDUINO_CFG)
	echo "Updating index..."
	$(CLI) core update-index
	echo "Installing core $(CORE)"
	$(CLI) core install $(CORE)
	cat ./libs.txt | xargs -E \n $(CLI) lib install

freeze-libs:
	arduino-cli lib list | awk -F ' [0-9]+' 'NR>1{print $$1}' \
		| sed -E 's/[[:space:]]*$$//' \
		| sed -E 's/[[:space:]]/\\ /g' > libs.txt

clear:
	if [ -d "$(CURDIR)/compile_commands.json" ]; then \
		rm -rf $(BUILDDIR)
	fi
	if [ -e "$(CURDIR)/compile_commands.json" ]; then \
		rm $(CURDIR)/compile_commands.json
	fi
