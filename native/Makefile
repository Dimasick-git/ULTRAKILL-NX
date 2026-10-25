ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO to the installed devkitPro SDK)
endif
SRCROOT ?= $(CURDIR)
TARGET := ultrakill_nx_native
OUTPUT := $(CURDIR)/$(TARGET)
APP_TITLE := ULTRAKILL NX Native - WIP
APP_AUTHOR := Dimasick-git
APP_VERSION := native-dev13
APP_ICON := $(SRCROOT)/../forwarder/icon.jpg
include $(DEVKITPRO)/libnx/switch_rules
LD := $(CXX)
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CPPFLAGS := -I$(SRCROOT)/include -isystem $(LIBNX)/include -D__SWITCH__
CXXFLAGS := -g -O2 -Wall -Wextra -std=gnu++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections $(ARCH)
LDFLAGS := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
PHYSICS_BUILD ?= $(CURDIR)/physics-switch
LIBS := -lNxGameplay -lNxPhysics -lJolt -ldeko3dd -lnx -lm
LIBPATHS := -L$(PHYSICS_BUILD) -L$(LIBNX)/lib
NROFLAGS := --nacp=$(TARGET).nacp --icon=$(APP_ICON) --romfsdir=$(CURDIR)/romfs
OFILES := main.o nx_gpu_material.o
.PHONY: all gameplay
all: $(TARGET).nro gameplay
gameplay: libnxgame.a
libnxgame.a: nx_movement.o nx_ground.o
	$(AR) rcs $@ $^
nx_movement.o: $(SRCROOT)/source/nx_movement.cpp $(SRCROOT)/include/nx_movement.h $(SRCROOT)/include/nx_scene.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -ffp-contract=off -MMD -MP -c $< -o $@
nx_ground.o: $(SRCROOT)/source/nx_ground.cpp $(SRCROOT)/include/nx_ground.h $(SRCROOT)/include/nx_physics_pack.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -ffp-contract=off -MMD -MP -c $< -o $@
$(TARGET).nro: $(TARGET).elf $(TARGET).nacp romfs/shaders/owned_world_vsh.dksh romfs/shaders/material_fsh.dksh romfs/level1-2.nxrender romfs/level1-2.nxmat romfs/level1-2.nxphys romfs/level1-2.nxgraph romfs/level1-2.nxactor romfs/level1-2.nxjoint romfs/licenses/JoltPhysics.txt
$(TARGET).elf: $(OFILES) $(PHYSICS_BUILD)/libNxGameplay.a $(PHYSICS_BUILD)/libNxPhysics.a $(PHYSICS_BUILD)/libJolt.a
main.o: $(SRCROOT)/source/main.cpp $(SRCROOT)/include/nx_scene.h $(SRCROOT)/include/nx_clock.h $(SRCROOT)/include/nx_session.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
nx_gpu_material.o: $(SRCROOT)/source/nx_gpu_material.cpp $(SRCROOT)/include/nx_gpu_material.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
romfs/level1-2.nxphys: migration/level1-2.nxphys
	cp $< $@
romfs/level1-2.nxgraph: migration/level1-2.nxgraph
	cp $< $@
romfs/level1-2.nxrender: migration/level1-2.nxrender
	cp $< $@
romfs/level1-2.nxmat: migration/level1-2.nxmat
	cp $< $@
romfs/level1-2.nxactor: migration/level1-2.nxactor
	cp $< $@
romfs/level1-2.nxjoint: migration/level1-2.nxjoint
	cp $< $@
romfs/licenses/JoltPhysics.txt: $(SRCROOT)/licenses/JoltPhysics.txt
	mkdir -p romfs/licenses
	cp $< $@
romfs/shaders/%_vsh.dksh: $(SRCROOT)/shaders/%_vsh.glsl
	mkdir -p romfs/shaders
	uam -s vert -o $@ $<
romfs/shaders/%_fsh.dksh: $(SRCROOT)/shaders/%_fsh.glsl
	mkdir -p romfs/shaders
	uam -s frag -o $@ $<
-include main.d nx_gpu_material.d nx_movement.d nx_ground.d
