DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
export DEVKITPRO
export DEVKITARM
TARGET := gbasync
BUILD := build
SOURCES := source/main.c

TOOLS := $(DEVKITPRO)/tools/bin
PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
CC := $(PREFIX)gcc

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CPPFLAGS := -I$(DEVKITPRO)/libctru/include -I$(DEVKITPRO)/portlibs/3ds/include
CFLAGS := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)
LDFLAGS := -specs=3dsx.specs $(ARCH) \
	-L$(DEVKITPRO)/libctru/lib -L$(DEVKITPRO)/portlibs/3ds/lib \
	-Wl,-Map,$(TARGET).map
LIBS := -lctru -lm

APP_TITLE := SaveSync
APP_DESCRIPTION := GBA save sync client
APP_AUTHOR := alexkirst

OBJ := $(BUILD)/main.o

.PHONY: all clean

all: $(TARGET).3dsx

$(BUILD):
	@mkdir -p $(BUILD)

$(OBJ): $(SOURCES) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $^ $(LDFLAGS) $(LIBS) -o $@

$(TARGET).smdh:
	$(TOOLS)/smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(DEVKITPRO)/libctru/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	$(TOOLS)/3dsxtool $(TARGET).elf $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).map
