# ZXEm top-level: delegate to emulator/.
.PHONY: all test tests verify clean release

all:
	$(MAKE) -C emulator

test tests:
	$(MAKE) -C emulator test

verify:
	$(MAKE) -C emulator verify

clean:
	$(MAKE) -C emulator clean

release:
	$(MAKE) -C emulator release
