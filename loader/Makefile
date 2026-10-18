#---------------------------------------------------------------------------------
# ultrakill_nx -- загрузчик Android-сборки ULTRAKILL (Unity 2022.3.62f2) под Horizon
# Requires devkitA64 + devkitPro pkgs: switch-mesa switch-libdrm_nouveau
#                                      switch-sdl2 switch-zlib
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO in your environment. (export DEVKITPRO=/opt/devkitpro)")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET    := ultrakill_nx
APP_TITLE := ULTRAKILL NX
APP_AUTHOR := Dimasick-git (loader: ChanseyIsTheBest, fgsfds, TheOfficialFloW)
APP_VERSION := 0.1.0
APP_ICON  := $(if $(wildcard $(TOPDIR)/icon.jpg),$(TOPDIR)/icon.jpg,$(DEVKITPRO)/libnx/default_icon.jpg)
export APP_TITLE APP_AUTHOR APP_VERSION APP_ICON
BUILD     := build
SOURCES   := source
INCLUDES  := source

ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) \
           $(INCLUDE) -D__SWITCH__
CFLAGS  += -DLOAD_ADDRESS=0xC0000000
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) \
           -Wl,--wrap,nwindowDequeueBuffer -Wl,--wrap,nwindowQueueBuffer -Wl,--wrap,nwindowCancelBuffer

# mesa GLES3 + EGL + nouveau, SDL2 for window/HID/audio, zlib. No ffmpeg/freetype
# (no video; soft-keyboard text rendering stubbed initially).
LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lpng -lturbojpeg -lz -lnx -lm

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

# NOTE: rename main_skeleton.c -> main.c once you've filled in the >>> REVERSE <<<
# entry points. Reuse these from cr3_nx: so_util.c libc_shim.c imports.c
# opensles.c jni_fake.c android_native.c error.c util.c
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CXX)
export OFILES := $(addsuffix .o,$(SFILES)) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(PORTLIBS)/include/SDL2 -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
# elf2nro (from switch_rules' %.nro recipe) uses $(NROFLAGS); without these the
# NRO gets no icon and no .nacp -> no author/title/version. The .nacp is already
# built (APP_TITLE/APP_AUTHOR/APP_VERSION) as a dependency; pass it + the icon.
NROFLAGS := --icon=$(APP_ICON) --nacp=$(OUTPUT).nacp
DEPENDS := $(OFILES:.o=.d)
all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)
-include $(DEPENDS)
endif
