# AMS_ROOT — путь к исходникам Atmosphere с собранной libstratosphere.
TID      := 4200000000525652
OUT_DIR  := out/nintendo_nx_arm64_armv8a/release
SD_DIR   := sd
TEST_DIR := tests/bin
HOST_CXX ?= g++
TESTS    := $(basename $(notdir $(wildcard tests/*.cpp)))

all: check-ams
	$(MAKE) -f system_module.mk AMS_ROOT=$(AMS_ROOT) ATMOSPHERE_CHECKED_LIBSTRATOSPHERE=1 \
		ATMOSPHERE_GIT_BRANCH=local ATMOSPHERE_GIT_REVISION=local ATMOSPHERE_GIT_HASH=0000000000000000

check-ams:
	@test -n "$(AMS_ROOT)" || { echo "AMS_ROOT не задан: make AMS_ROOT=/путь/к/Atmosphere"; exit 1; }

test-app:
	$(MAKE) -C test-app

test:
	@mkdir -p $(TEST_DIR)
	@for t in $(TESTS); do \
		$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -Werror tests/$$t.cpp -o $(TEST_DIR)/$$t && \
		./$(TEST_DIR)/$$t && echo "ok   $$t" || { echo "FAIL $$t"; exit 1; }; \
	done

dist: all test-app
	rm -rf $(SD_DIR)
	mkdir -p $(SD_DIR)/atmosphere/contents/$(TID)/flags $(SD_DIR)/config/ryz-lite-rumble $(SD_DIR)/switch/ryz-rumble-test
	cp $(OUT_DIR)/ryz-lite-rumble-mitm.nsp $(SD_DIR)/atmosphere/contents/$(TID)/exefs.nsp
	touch $(SD_DIR)/atmosphere/contents/$(TID)/flags/boot2.flag
	printf 'audctl\n' > $(SD_DIR)/atmosphere/contents/$(TID)/mitm.lst
	printf '{\n  "name": "Ryz Lite Rumble MITM",\n  "tid": "$(TID)",\n  "requires_reboot": true\n}\n' > $(SD_DIR)/atmosphere/contents/$(TID)/toolbox.json
	cp config/config.ini $(SD_DIR)/config/ryz-lite-rumble/config.ini
	cp test-app/ryz-rumble-test.nro $(SD_DIR)/switch/ryz-rumble-test/

clean:
	rm -rf out build $(SD_DIR) $(TEST_DIR) test-app/build test-app/*.nro test-app/*.elf test-app/*.nacp

.PHONY: all check-ams test-app test dist clean
