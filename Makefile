UNAME := $(shell uname -s)

ifeq ($(UNAME), Linux)
debug:
	mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && $(MAKE) -j$(nproc) -s

release:
	mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j$(nproc) -s

clear:
	-rm -r build/CMakeFiles
	-rm build/CMakeCache.txt
	-rm build/cmake_install.cmake
else
debug:
	-mkdir build
	cd build && cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug .. && $(MAKE) -j$(nproc) -s

release:
	-mkdir build
	cd build && cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release .. && $(MAKE) -j$(nproc) -s

clear:
	-rmdir /s /q build/CMakeFiles
	-del build/CMakeCache.txt
	-del build/cmake_install.cmake
endif

clean:
	$(MAKE) -s -C build clean

run:
	$(MAKE) -j$(nproc) -s -C build run
