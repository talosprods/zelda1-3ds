#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
GME			:=	../Loz/Tools/ExtractNsf/Game_Music_Emu/gme
# The ROM extractor (RomExtract) lives with the PC patcher in patcher/src/extract: the
# game runs the same code on the console for the ROM-on-SD route. The patcher
# folder sits next to this one in the development checkout and one level up
# in the public repository (game/zelda3ds next to patcher/). Resolved once in
# the outer make and exported, since the inner make runs from build/.
PATCHER_SRC	?=	$(firstword $(wildcard ../patcher/src/extract ../../patcher/src/extract))
ifeq ($(strip $(PATCHER_SRC)),)
$(error "patcher/src/extract (RomExtract) not found next to or above this folder")
endif
export PATCHER_SRC
SOURCES		:=	source ../Loz/Game/Loz $(GME) $(PATCHER_SRC)
DATA		:=	data
INCLUDES	:=	source ../Loz/Game/Loz $(GME) $(PATCHER_SRC)
ROMFS		:=	romfs

APP_TITLE		:=	Zelda 3DS
APP_DESCRIPTION	:=	The Legend of Zelda for 3DS
APP_AUTHOR		:=	Zelda 3DS Edition (fan port)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# Force-include the MSVC->POSIX shims (fopen_s, _countof, _stricmp, _mkdir) that
# the shared game code relies on; devkitARM's newlib doesn't provide them.
COMPAT	:=	-include $(TOPDIR)/../Loz/Game/Loz/msvc_compat.h

# -ffile-prefix-map keeps the builder's absolute paths out of __FILE__
# (assert messages) in the binary.
CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			-ffile-prefix-map=$(abspath $(TOPDIR)/..)=. \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ $(COMPAT) -Wno-narrowing -Wno-unused-variable

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++14

ASFLAGS	:=	-g $(ARCH)
# --gc-sections drops the GME modules we don't reference (only Nsf_Emu's chain
# is pulled in) despite compiling the whole gme/ dir (simplest to keep in sync).
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections

LIBS	:= -lcitro2d -lcitro3d -lctru -lm

LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o)

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
	# The stock devkitPro rules build the .3dsx from the .elf (+ .smdh) only --
	# the romfs directory is passed to 3dsxtool as a *flag*, never declared as a
	# prerequisite. So changing an asset and nothing else left `make` reporting
	# "zelda3ds.3dsx is up to date" and shipping the previously embedded copy of
	# romfs, silently. That once cost a whole debugging session: the
	# bottom-screen terrain atlas (romfs/terrain.t3x) was regenerated correctly
	# several times, but only reached the binary on the runs where a .cpp
	# happened to change too, so builds kept being tested with the old atlas
	# and kept seeing a bug that was already fixed in the source. Listing the
	# romfs contents as real prerequisites makes an asset-only change rebuild.
	export ROMFS_DEPS := $(shell find $(CURDIR)/$(ROMFS) -type f)
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

# (The terrain.t3x atlas rule that used to live here is gone: the live map's
# terrain thumbnails are now built on the console from the player's ROM, see
# RomExtract.cpp BuildTerrain() and Graphics_3ds.cpp BuildTerrainTexture().)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS) $(ROMFS_DEPS)
$(OFILES_SOURCES) : $(HFILES)
$(OUTPUT).elf	:	$(OFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
