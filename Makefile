UNAME := $(shell uname -s)

ifeq ($(UNAME), Linux)
debug:
	mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && $(MAKE) -j$(nproc) -s

release:
	mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j$(nproc) -s

clear:
	-rm -r build/CMakeFiles
	-rm -r build/_deps
	-rm build/CMakeCache.txt
	-rm build/cmake_install.cmake

run:
	gnome-terminal -- zsh -c "cd bin && ./main; exec zsh"

else
debug:
	-mkdir build
	cd build && cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug .. && $(MAKE) -j$(nproc) -s

release:
	-mkdir build
	cd build && cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j$(nproc) -s

clear:
	-rmdir /s /q build/CMakeFiles
	-rmdir /s /q build/_deps
	-del build/CMakeCache.txt
	-del build/cmake_install.cmake

run:
	cd bin && ./main.exe

endif

EMXX = em++
EMSRC = src/main.cpp src/NetworkCore.cpp src/PeerManager.cpp
EMSCRIPTEN_FLAGS = -w -fms-extensions -std=c++17 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -O3 lib/libenet-Web.a -Iinclude -DPLATFORM_WEB --shell-file=lib/shell.html -s USE_GLFW=3 -s ASSERTIONS=2 -s WASM=1 -s ASYNCIFY -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_MEMORY=67108864 -s FORCE_FILESYSTEM=1 -s EXPORTED_FUNCTIONS=["_free","_malloc","_main"] -s EXPORTED_RUNTIME_METHODS=ccall
EMSCRIPTEN_FLAGS += -lwebsocket.js
# EMSCRIPTEN_FLAGS += -sUSE_PTHREADS=1 -sPROXY_POSIX_SOCKETS -sPROXY_TO_PTHREAD
# EMSCRIPTEN_FLAGS += -sSOCKET_DEBUG -sWEBSOCKET_DEBUG -sSYSCALL_DEBUG

web: $(EMSRC)
	$(EMXX) $(EMSRC) $(EMSCRIPTEN_FLAGS) -o bin/main.html

clean:
	$(MAKE) -s -C build clean
