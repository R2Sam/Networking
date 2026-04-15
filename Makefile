# ======================
# Basic setup
# ======================

BUILD_DIR ?= build
SRC_DIR   ?= .
JOBS      := $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

.PHONY: configure debug release tests build clean format fix fixChanges

# ======================
# Platform detection
# ======================

CMAKE_GENERATOR :=

ifeq ($(OS), Windows_NT)
	CMAKE_GENERATOR := -G "MinGW Makefiles"
endif

debug:
	cmake -S $(SRC_DIR) -B $(BUILD_DIR) \
	-DFETCHCONTENT_BASE_DIR=../.deps \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	$(CMAKE_GENERATOR)
	cmake --build $(BUILD_DIR) -j $(JOBS)

release:
	cmake -S $(SRC_DIR) -B $(BUILD_DIR) \
		-DFETCHCONTENT_BASE_DIR=../.deps \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		$(CMAKE_GENERATOR)
	cmake --build $(BUILD_DIR) -j $(JOBS)

tests:
	cmake -S $(SRC_DIR) -B $(BUILD_DIR) \
		-DFETCHCONTENT_BASE_DIR=../.deps \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_TESTING=ON \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		$(CMAKE_GENERATOR)
	cmake --build $(BUILD_DIR) -j $(JOBS)
	cd bin && ./TestsNetworking --allow-running-no-tests

# ======================
# Clean (CMake-safe)
# ======================

clean:
	cmake --build $(BUILD_DIR) --target clean

# ======================
# Formatting / Tidy
# ======================

format:
	find $(CURDIR)/src -type f \( -name "*.cpp" -o -name "*.hpp" \) \
	| xargs -P$(JOBS) clang-format --dry-run --Werror
	run-clang-tidy -quiet -j$(JOBS) -p $(BUILD_DIR) $(CURDIR)/src

fix:
	run-clang-tidy -quiet -fix -j$(JOBS) -p $(BUILD_DIR) $(CURDIR)/src
	find $(CURDIR)/src -type f \( -name "*.cpp" -o -name "*.hpp" \) \
	| xargs -P$(JOBS) clang-format -i --Werror

fixChanges:
	git diff --cached --name-only --diff-filter=d \
	| grep -E '\.(cpp|hpp)$$' \
	| xargs -r run-clang-tidy -quiet -fix -j$(JOBS) -p $(BUILD_DIR)

	git diff --cached --name-only --diff-filter=d \
	| grep -E '\.(cpp|hpp)$$' \
	| xargs -r -P$(JOBS) clang-format -i --Werror