PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

WOLFSSL_DIR ?= ../wolfssl-5.9.4-stable
WOLFSSH_DIR ?= ../wolfssh-1.6.0-stable
SHSRV_DIR   ?= ../shsrv

BUILD := build
ELF   := ps5sshd.elf

CPPFLAGS := -DWOLFSSL_USER_SETTINGS -Iinclude -I$(WOLFSSL_DIR) -I$(WOLFSSH_DIR)
PREFIX_MAP := -ffile-prefix-map=$(abspath ..)/= -ffile-prefix-map=$(abspath $(PS5_PAYLOAD_SDK))/=sdk/

CFLAGS   := -O2 -g -Wall $(PREFIX_MAP)
LIBCFLAGS := $(CFLAGS) -Wno-unused-function -Wno-unused-but-set-variable

WOLFCRYPT_SRCS := aes.c asn.c coding.c cpuid.c curve25519.c dh.c ecc.c \
                  ed25519.c error.c fe_operations.c ge_operations.c hash.c \
                  hmac.c kdf.c logging.c memory.c random.c rsa.c sha.c \
                  sha256.c sha3.c sha512.c signature.c sp_int.c sp_x86_64.c \
                  wc_encrypt.c wc_mlkem.c wc_mlkem_poly.c wc_port.c \
                  wolfmath.c wolfentropy.c
WOLFCRYPT_ASMS := aes_x86_64_asm.S aes_gcm_asm.S sp_x86_64_asm.S

WOLFSSH_SRCS := internal.c io.c keygen.c log.c ossh.c port.c ssh.c wolfscp.c \
                wolfsftp.c wolfterm.c

WOLFCRYPT_OBJS := $(addprefix $(BUILD)/wolfcrypt/,$(WOLFCRYPT_SRCS:.c=.o)) \
                  $(addprefix $(BUILD)/wolfcrypt/,$(WOLFCRYPT_ASMS:.S=.o))
WOLFSSH_OBJS   := $(addprefix $(BUILD)/wolfssh/,$(WOLFSSH_SRCS:.c=.o))

LIBWOLFCRYPT := $(BUILD)/libwolfcrypt.a
LIBWOLFSSH   := $(BUILD)/libwolfssh.a

SHSRV_BUNDLES := core http2_get launch suspend hbldr pkg_install
SHSRV_LIBS    := $(foreach b,$(SHSRV_BUNDLES),$(SHSRV_DIR)/bundles/$(b)/lib.a)

APP_SRCS := main.c session.c shell.c config.c util.c compat.c sftp_io.c
APP_OBJS := $(addprefix $(BUILD)/app/,$(APP_SRCS:.c=.o)) \
            $(BUILD)/shsrv/elfldr.o $(BUILD)/shsrv/pt.o

PS5SH_OBJS := $(BUILD)/sh/ps5sh.o $(BUILD)/sh/chmod.o $(BUILD)/sh/rm.o $(BUILD)/shsrv/builtin.o \
              $(BUILD)/shsrv/elfldr.o $(BUILD)/shsrv/pt.o

all: $(ELF)

STRIP_FLAGS := --strip-debug --remove-section=.comment

$(BUILD)/ps5sshd.debug.elf: $(APP_OBJS) $(LIBWOLFSSH) $(LIBWOLFCRYPT)
	$(CC) -o $@ $(APP_OBJS) $(LIBWOLFSSH) $(LIBWOLFCRYPT) -lkernel_sys

$(ELF): $(BUILD)/ps5sshd.debug.elf
	$(STRIP) $(STRIP_FLAGS) -o $@ $<

$(BUILD)/app/%.o: src/%.c src/ps5sshd.h include/user_settings.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -I$(BUILD) -I$(SHSRV_DIR) $(CFLAGS) -Werror -c -o $@ $<

$(BUILD)/app/shell.o: $(BUILD)/ps5sh.elf.inc

$(BUILD)/shsrv/%.o: $(SHSRV_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/sh/%.o: sh/%.c
	@mkdir -p $(dir $@)
	$(CC) -I$(SHSRV_DIR) $(CFLAGS) -Werror -c -o $@ $<

$(SHSRV_DIR)/bundles/%/lib.a:
	$(MAKE) -C $(SHSRV_DIR)/bundles/$*

$(BUILD)/ps5sh.debug.elf: $(PS5SH_OBJS) $(SHSRV_LIBS)
	$(CC) -lkernel_sys -o $@ $(PS5SH_OBJS) \
	      $(foreach l,$(SHSRV_LIBS),-Wl,--whole-archive $(l))

$(BUILD)/ps5sh.elf: $(BUILD)/ps5sh.debug.elf
	$(STRIP) $(STRIP_FLAGS) -o $@ $<

$(BUILD)/ps5sh.elf.inc: $(BUILD)/ps5sh.elf
	cd $(BUILD) && xxd -i ps5sh.elf > ps5sh.elf.inc

$(BUILD)/wolfcrypt/%.o: $(WOLFSSL_DIR)/wolfcrypt/src/%.c include/user_settings.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(LIBCFLAGS) -c -o $@ $<

$(BUILD)/wolfcrypt/%.o: $(WOLFSSL_DIR)/wolfcrypt/src/%.S include/user_settings.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/wolfssh/%.o: $(WOLFSSH_DIR)/src/%.c include/user_settings.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(LIBCFLAGS) -c -o $@ $<

# wolfsftp.c is built from a patched copy (see patches/) with the SFTP write
# batching hooks from src/sftp_io.h.
WOLFSSH_PATCHES := $(sort $(wildcard patches/wolfssh-*.patch))

$(BUILD)/wolfssh-patched/src/wolfsftp.c: $(WOLFSSH_DIR)/src/wolfsftp.c $(WOLFSSH_PATCHES)
	@mkdir -p $(dir $@)
	cp $< $@
	for p in $(WOLFSSH_PATCHES); do patch -s -p1 -d $(BUILD)/wolfssh-patched < $$p || exit 1; done

$(BUILD)/wolfssh/wolfsftp.o: $(BUILD)/wolfssh-patched/src/wolfsftp.c include/user_settings.h src/sftp_io.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(LIBCFLAGS) -include src/sftp_io.h -c -o $@ $<

$(LIBWOLFCRYPT): $(WOLFCRYPT_OBJS)
	$(AR) rcs $@ $^

$(LIBWOLFSSH): $(WOLFSSH_OBJS)
	$(AR) rcs $@ $^

libs: $(LIBWOLFCRYPT) $(LIBWOLFSSH)

clean:
	rm -rf $(BUILD) $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

.PHONY: all libs clean test
