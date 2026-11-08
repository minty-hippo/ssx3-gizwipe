# SPDX-License-Identifier: CC0-1.0
#
# SPDX-FileContributor: Antonio Niño Díaz, 2025

BLOCKSDS	?= /opt/blocksds/core

# User config

NAME		:= ssx3_gizwipe
GAME_TITLE	:= SSX 3 GizWipe
GAME_SUBTITLE	:= Gizmondo -> DS port test

# Source code paths

NITROFSDIR		= nitrofs

# local wireless multiplayer (stage 41): the ARM7 core with DSWifi, the IP-less DSWifi (local mode only)
ARM7ELF		:= $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi_maxmod.elf
LIBS		:= -ldswifi9_noip -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile

# test builds: make TESTDEFS="-DTEST_LOG ..." (passing DEFINES= on the command line replaced the default makefile's
# -DARM9 -D__NDS__ as well)
CFLAGS		+= $(TESTDEFS)

# the per-frame hot code is built as ARM, not Thumb: the ARM946E-S has no long multiply (SMULL) in Thumb-1, so every
# s64 product in the fixed-point physics, collision, skinning and culling was a library call
ARM_HOT		:= physics tricks rails track rider ghost bot hud main fireworks sky music voicefx
$(foreach f,$(ARM_HOT),$(eval $(BUILDDIR)/source/$(f).c.o: CFLAGS += -marm))
