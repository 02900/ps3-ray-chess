#---------------------------------------------------------------------------------
# RayChess PS3 - Makefile
#
# A PS3 port of ray-chess (github.com/GustavoHenriqueMuller/ray-chess), an
# open-source raylib chess game, on PSL1GHT + RSXGL via raylib. Build with the
# raylib toolchain image (it stacks raylib on top of the RSXGL image):
#   docker run --rm -v "$PWD":/src -w /src ghcr.io/02900/ps3-toolchain-raylib make
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

#---------------------------------------------------------------------------------
# Application metadata
#---------------------------------------------------------------------------------
TITLE		:=	RayChess
APPID		:=	RAYCHESS
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
ICON0		:=	pkgfiles/ICON0.PNG
SFOXML		:=	sfo.xml

include $(PSL1GHT)/ppu_rules

#---------------------------------------------------------------------------------
# Directories
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source source/pieces
DATA		:=	data
INCLUDES	:=	include
PKGFILES	:=	pkgfiles

#---------------------------------------------------------------------------------
# Libraries to link
# raylib over the RSXGL OpenGL stack: -lraylib first (it references the EGL/GL/RSX
# symbols), then RSXGL (-lEGL -lGL) and the PSL1GHT RSX/gcm libs. RSXGL underneath
# is C++, so the final link MUST use the C++ driver (see LD override below) to pull
# in libstdc++ (operator new/delete, __cxa_*, std::*, boost exceptions).
#---------------------------------------------------------------------------------
LIBS		:=	-lraylib -lEGL -lGL \
			-lrsx -lgcm_sys -lnet -lio -lsysutil -lsysmodule \
			-lrt -llv2 -lpng -lz -lm -lmikmod -laudio

#---------------------------------------------------------------------------------
# Compiler flags. -D__RSX__ is required by the RSXGL headers.
#---------------------------------------------------------------------------------
CFLAGS		=	-O2 -Wall -mcpu=cell -std=gnu99 -D__RSX__ $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	-O2 -Wall -mcpu=cell -std=gnu++17 -D__RSX__ $(MACHDEP) $(INCLUDE)
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# Library directories
#---------------------------------------------------------------------------------
LIBDIRS	:=

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
export BUILDDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))
PNGFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.png)))

# RSXGL's libGL is C++, so always link with the C++ driver even when our own
# sources are pure C — otherwise libstdc++ symbols are left undefined.
export LD	:=	$(CXX)

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(addsuffix .o,$(PNGFILES)) \
			$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			$(LIBPSL1GHT_INC) \
			-I$(CURDIR)/$(BUILD) -I$(PORTLIBS)/include

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
			$(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib

export OUTPUT	:=	$(CURDIR)/$(TARGET)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo Cleaning...
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).self EBOOT.BIN *.pkg

run: $(BUILD)
	ps3load $(OUTPUT).self

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

%.bin.o: %.bin
	@echo $(notdir $<)
	@$(bin2o)

%.png.o: %.png
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
