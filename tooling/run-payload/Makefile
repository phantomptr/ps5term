# PS5Term - ps5term-run payload and its terminal test program. GPL-3.0-or-later.
# Built by tools/build-run-payload.sh with the pinned PS5 payload SDK.
ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

SHSRV := ../../third_party/shsrv
OUT ?= .
CFLAGS := -Wall -Werror -O2

all: $(OUT)/ps5term-run.elf $(OUT)/ps5term-term-test.elf

$(OUT)/ps5term-run.elf: ps5term-run.c run_request.c $(SHSRV)/elfldr.c $(SHSRV)/pt.c
	$(CC) $(CFLAGS) -I$(SHSRV) -lkernel_sys -o $@ $^

$(OUT)/ps5term-term-test.elf: test/term-test.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: all
