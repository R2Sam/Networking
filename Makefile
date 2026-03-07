UNAME := $(shell uname -s)
JOBS := $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

ifeq ($(UNAME), Linux)
debug:
	mkdir -p build && cmake -B build -DFETCHCONTENT_BASE_DIR=../.deps -DCMAKE_BUILD_TYPE=Debug  && cd build && $(MAKE) -j${JOBS} -s
	git diff -U0 HEAD^ | clang-format-diff -p1
	git diff --name-only | grep .cpp | xargs -r run-clang-tidy -quiet -j${JOBS} -p build
	

release:
	mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j${JOBS} -s

clear:
	-mv build/compile_commands.json compile_commands.json
	-rm -r build
	-mkdir build
	-mv compile_commands.json build/compile_commands.json
	
format:
	find src -name "*.cpp" -o -name "*.h" | xargs -P${JOBS} clang-format --dry-run --Werror
	run-clang-tidy -quiet -j${JOBS} -p build $(CURDIR)/src/

fix:
	find src -name "*.cpp" -o -name "*.h" | xargs -P${JOBS} clang-format -i --Werror
	run-clang-tidy -quiet -fix -j${JOBS} -p build $(CURDIR)/src/

run:
	gnome-terminal -- zsh -c "cd bin && ./main; exec zsh"

else
debug:
	-mkdir build
	cd build && cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug .. && $(MAKE) -j12 -s

release:
	-mkdir build
	cd build && cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j12 -s

clear:
	-mv build/compile_commands.json compile_commands.json
	-rmdir /s /q build
	-mkdir build
	-mv compile_commands.json build/compile_commands.json

run:
	cd bin && ./main.exe

endif

clean:
	$(MAKE) -s -C build clean