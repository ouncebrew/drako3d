#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

export DRAKO3D_MAJOR	:= 0
export DRAKO3D_MINOR	:= 5
export DRAKO3D_PATCH	:= 0


VERSION	:=	$(DRAKO3D_MAJOR).$(DRAKO3D_MINOR).$(DRAKO3D_PATCH)

#---------------------------------------------------------------------------------
# GPU_ARCH selects the target GPU (ampere [GA10F, Switch 2] or maxwell [GM20B, Switch 1])
# Run "make clean" after switching
# The ampere build uses libounce ($(DEVKITPRO)/libounce) instead of libnx
#---------------------------------------------------------------------------------
GPU_ARCH	?=	ampere

ifeq ($(GPU_ARCH),ampere)
include $(DEVKITPRO)/libounce/switch2_rules
GPU_DIRS	:=	source/ampere source/maxwell
GPU_DEFINES	:=	-DDK_GPU_AMPERE=1
else ifeq ($(GPU_ARCH),maxwell)
include $(DEVKITPRO)/libnx/switch_rules
GPU_DIRS	:=	source/maxwell
else
$(error "Unknown GPU_ARCH '$(GPU_ARCH)', expected ampere or maxwell")
endif

#---------------------------------------------------------------------------------
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------
#BUILD		:=	build
SOURCES		:=	$(GPU_DIRS) source
DATA		:=	data
INCLUDES	:=	include $(GPU_DIRS) source

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec

CFLAGS	:=	-g -Wall -Werror \
			-ffunction-sections \
			-fdata-sections \
			-fmacro-prefix-map=$(TOPDIR)/source/= \
			$(ARCH) \
			$(BUILD_CFLAGS)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -D__DK_INTERNAL__ $(GPU_DEFINES)

# Extra optimizations for the Release build. This list is equivalent to -O3,
# with opts that hurt code size (i.e. aggressive loop unrolling) disabled.
EXTRAOPT	=	-fpredictive-commoning \
				-fgcse-after-reload \
				-ftree-loop-distribution \
				-ftree-loop-distribute-patterns \
				-ftree-vectorize \
				-floop-interchange \
				-fsplit-paths \
				-fvect-cost-model=cheap \
				-ftree-partial-pre

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(LIBNX)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES		:=	$(sort $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(sort $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))))
SFILES		:=	$(sort $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s))))
DEFFILES	:=	$(sort $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.def))))
MMEFILES	:=	$(sort $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.mme))))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES))) $(DEFFILES:.def=.h)
ifneq ($(strip $(MMEFILES)),)
export HFILES	+=	mme_macros.h
export MMEFILES
endif
ifeq ($(GPU_ARCH),ampere)
export HFILES	+=	method_names.h
export DEFFILES
export MMEFLAGS	:=	--isa=turing
endif

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I.

.PHONY: clean all

#---------------------------------------------------------------------------------
all: lib/libdrako3d.a lib/libdrako3dd.a

dist-bin: all
	@tar --exclude=*~ -cjf drako3d-$(VERSION).tar.bz2 include lib

dist-src:
	@tar --exclude=*~ -cjf drako3d-src-$(VERSION).tar.bz2 include source Makefile

dist: dist-src dist-bin

install: dist-bin
	mkdir -p $(DESTDIR)$(LIBNX)
	bzip2 -cd drako3d-$(VERSION).tar.bz2 | tar -xf - -C $(DESTDIR)$(LIBNX)

lib:
	@[ -d $@ ] || mkdir -p $@

release:
	@[ -d $@ ] || mkdir -p $@

debug:
	@[ -d $@ ] || mkdir -p $@

lib/libdrako3d.a : lib release $(SOURCES) $(INCLUDES)
	@$(MAKE) BUILD=release OUTPUT=$(CURDIR)/$@ \
	BUILD_CFLAGS="-DNDEBUG=1 -O2 $(EXTRAOPT)" \
	DEPSDIR=$(CURDIR)/release \
	--no-print-directory -C release \
	-f $(CURDIR)/Makefile

lib/libdrako3dd.a : lib debug $(SOURCES) $(INCLUDES)
	@$(MAKE) BUILD=debug OUTPUT=$(CURDIR)/$@ \
	BUILD_CFLAGS="-DDEBUG=1 -O2" \
	DEPSDIR=$(CURDIR)/debug \
	--no-print-directory -C debug \
	-f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr release debug lib

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT)	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES)

mme_macros.h : engine_3d.mme $(MMEFILES)
	@echo $(notdir $@)
	@drakomme $(MMEFLAGS) -o $@ $^

%_3d.h %_3d.mme : %_3d.def
	@echo $(notdir $<)
	@drakodef -h $*_3d.h -m $*_3d.mme $<

%.h : %.def
	@echo $(notdir $<)
	@drakodef -h $@ $<

method_names.h : $(DEFFILES) method_names_gen.py
	@echo $(notdir $@)
	@python3 $(filter %.py,$^) -o $@ $(filter %.def,$^)

#---------------------------------------------------------------------------------
%_bin.h %.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)


-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
