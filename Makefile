BUILD_DIR     ?= build
BUILD_ARM64   ?= build-aarch64
BUILD_WIN64   ?= build-win64
TOOLCHAIN_ARM ?= cmake/toolchain-aarch64-linux.cmake
TOOLCHAIN_WIN ?= cmake/toolchains/mingw-w64-x86_64.cmake
BUILD_TYPE    ?= Release
JOBS          ?= $(shell nproc)

PREFIX        ?= /usr/local
BINDIR        ?= $(PREFIX)/bin
LIBDIR        ?= $(PREFIX)/lib/thingino-dfu

.PHONY: all arm64 win64 bundle-win64 web install uninstall clean help

all:
	@mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && make -j$(JOBS)

arm64:
	@mkdir -p $(BUILD_ARM64) && cd $(BUILD_ARM64) && cmake .. -DCMAKE_TOOLCHAIN_FILE=../$(TOOLCHAIN_ARM) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && make -j$(JOBS)

win64:
	@./scripts/fetch-libusb-win.sh
	@mkdir -p $(BUILD_WIN64) && cd $(BUILD_WIN64) && cmake .. -DCMAKE_TOOLCHAIN_FILE=../$(TOOLCHAIN_WIN) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && make -j$(JOBS)

bundle-win64: win64
	@mkdir -p dist/thingino-dfu-windows-x64
	cp $(BUILD_WIN64)/cli/thingino-dfu.exe dist/thingino-dfu-windows-x64/
	cp $(BUILD_WIN64)/dfu-remote/dfu-remote.exe dist/thingino-dfu-windows-x64/
	cp $(BUILD_WIN64)/cli/libusb-1.0.dll dist/thingino-dfu-windows-x64/
	cp -r firmware dist/thingino-dfu-windows-x64/
	cd dist && zip -r thingino-dfu-windows-x64.zip thingino-dfu-windows-x64
	rm -rf dist/thingino-dfu-windows-x64
	@echo "Created dist/thingino-dfu-windows-x64.zip"

web:
	@./web/build.sh

install: all
	install -d $(DESTDIR)$(LIBDIR)
	install -m 755 $(BUILD_DIR)/cli/thingino-dfu $(DESTDIR)$(LIBDIR)/
	[ -f $(BUILD_DIR)/dfu-remote/dfu-remote ] && install -m 755 $(BUILD_DIR)/dfu-remote/dfu-remote $(DESTDIR)$(LIBDIR)/ || true
	install -d $(DESTDIR)$(LIBDIR)/firmware
	cp -r firmware/* $(DESTDIR)$(LIBDIR)/firmware/
	install -d $(DESTDIR)$(BINDIR)
	ln -sf ../lib/thingino-dfu/thingino-dfu $(DESTDIR)$(BINDIR)/thingino-dfu
	[ -f $(DESTDIR)$(LIBDIR)/dfu-remote ] && ln -sf ../lib/thingino-dfu/dfu-remote $(DESTDIR)$(BINDIR)/dfu-remote || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/thingino-dfu
	rm -f $(DESTDIR)$(BINDIR)/dfu-remote
	rm -rf $(DESTDIR)$(LIBDIR)

clean:
	rm -rf $(BUILD_DIR) $(BUILD_ARM64) $(BUILD_WIN64) build-arm64 web/build web/dist web/public/wasm

help:
	@echo "make          - build native (Linux/macOS)"
	@echo "make arm64    - cross-compile for aarch64 Linux"
	@echo "make win64    - cross-compile for Windows x64 (requires mingw-w64)"
	@echo "make bundle-win64 - build and package Windows x64 binaries into a zip"
	@echo "make web      - build WebAssembly (requires emsdk)"
	@echo "make install  - install to PREFIX (default: /usr/local)"
	@echo "make uninstall - remove installed files"
	@echo "make clean    - remove build directories"
