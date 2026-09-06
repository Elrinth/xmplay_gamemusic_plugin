# xmp-gamemusic — native XMPlay input plugin (Game Music)
#
#   /usr/bin/make          # host tests + 32-bit DLL
#   /usr/bin/make dll      # dist/xmp-gamemusic.dll
#   /usr/bin/make test     # host engine/render checks
#   /usr/bin/make pack     # /workspace/xmp-gamemusic-1.0.11.zip
#
# If `make` is a wrapper, invoke GNU make explicitly.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DIST := $(ROOT)/dist
SRC  := $(ROOT)/src
INC  := $(ROOT)/include/xmplay
TP   := $(ROOT)/third_party
OBJ  := $(DIST)/obj
OBJW := $(DIST)/obj-i686

I686_HOST := i686-w64-mingw32
I686_CC   := $(I686_HOST)-gcc
I686_CXX  := $(I686_HOST)-g++
I686_AR   := $(I686_HOST)-ar
I686_WINDRES := $(I686_HOST)-windres

JOBS ?= $(shell nproc 2>/dev/null || echo 2)
CC  := gcc
CXX := g++

NEZ := $(TP)/nezplug
GME := $(TP)/gme
NSF := $(TP)/nsfplay
FAT := $(TP)/fatso
LZ7 := $(TP)/lzma7z
PSF := $(TP)/psflib
VIO := $(TP)/viogsf
USF := $(TP)/lazyusf2
HA  := $(TP)/highly_advanced

INCS_COM = -I$(SRC) -I$(INC) -I$(TP) -I$(NEZ) -I$(NEZ)/format -I$(NEZ)/device \
	-I$(NEZ)/device/nes -I$(NEZ)/device/opl -I$(NEZ)/cpu -I$(NEZ)/cpu/kmz80 \
	-I$(NEZ)/cpu/km6502 -I$(GME) -I$(GME)/ext -I$(NSF) -I$(NSF)/xgm -I$(NSF)/contrib -I$(FAT) \
	-I$(TP)/lzma7z -I$(PSF) -I$(VIO) -I$(VIO)/vbam -I$(VIO)/vbam/gba -I$(VIO)/vbam/apu \
	-I$(USF)

HA_INCS = -I$(HA) -I$(HA)/VBA
CXXFLAGS_HA = $(HA_INCS) $(CFLAGS_COM) -std=c++14 -DC_CORE -DLINUX

CFLAGS_WARN = -w -Wno-incompatible-function-pointer-types -fpermissive

CFLAGS_COM = -O2 -fno-strict-aliasing -fcommon -DNDEBUG -D__forceinline=inline \
	$(CFLAGS_WARN) $(INCS_COM)
CXXFLAGS_GME = $(CFLAGS_COM) -std=c++11 -fno-exceptions -fno-rtti \
	-DGME_DISABLE_STEREO_DEPTH=1 -DVGM_YM2612_NUKED -DBLARGG_LITTLE_ENDIAN=1
CXXFLAGS_NSF = $(CFLAGS_COM) -std=c++17 -include $(NSF)/emu2413_rename.h
CFLAGS_NSF = $(CFLAGS_COM) -include $(NSF)/emu2413_rename.h
CXXFLAGS_FAT = $(CFLAGS_COM) -std=c++14 -D__forceinline=inline \
	-include $(FAT)/compat/fatso_minmax.h -I$(FAT)/compat
CFLAGS_FAT = $(CFLAGS_COM) -I$(FAT)/compat -include $(FAT)/compat/fatso_minmax.h
CXXFLAGS_PLUG = $(CFLAGS_COM) -std=c++14

NEZ_SRCS = \
	$(NEZ)/format/nezplug.c \
	$(NEZ)/format/audiosys.c \
	$(NEZ)/format/songinfo.c \
	$(NEZ)/format/handler.c \
	$(NEZ)/format/m_nsf.c \
	$(NEZ)/format/m_gbr.c \
	$(NEZ)/format/m_kss.c \
	$(NEZ)/format/m_hes.c \
	$(NEZ)/format/m_zxay.c \
	$(NEZ)/format/m_sgc.c \
	$(NEZ)/format/m_nsd.c \
	$(NEZ)/format/nsf6502.c \
	$(NEZ)/cpu/kmz80/kmz80.c \
	$(NEZ)/cpu/kmz80/kmdmg.c \
	$(NEZ)/cpu/kmz80/kmr800.c \
	$(NEZ)/cpu/kmz80/kmevent.c \
	$(NEZ)/cpu/kmz80/kmz80c.c \
	$(NEZ)/cpu/kmz80/kmz80t.c \
	$(NEZ)/device/s_psg.c \
	$(NEZ)/device/s_scc.c \
	$(NEZ)/device/s_hes.c \
	$(NEZ)/device/s_hesad.c \
	$(NEZ)/device/s_dmg.c \
	$(NEZ)/device/s_sng.c \
	$(NEZ)/device/s_logtbl.c \
	$(NEZ)/device/nes/s_apu.c \
	$(NEZ)/device/nes/s_fds.c \
	$(NEZ)/device/nes/s_fds1.c \
	$(NEZ)/device/nes/s_fds2.c \
	$(NEZ)/device/nes/s_fds3.c \
	$(NEZ)/device/nes/s_mmc5.c \
	$(NEZ)/device/nes/s_vrc6.c \
	$(NEZ)/device/nes/s_vrc7.c \
	$(NEZ)/device/nes/s_n106.c \
	$(NEZ)/device/nes/s_fme7.c \
	$(NEZ)/device/nes/logtable.c \
	$(NEZ)/device/opl/s_opl.c \
	$(NEZ)/device/opl/s_opltbl.c \
	$(NEZ)/device/opl/s_deltat.c

GME_SRCS = \
	$(GME)/Blip_Buffer.cpp \
	$(GME)/Classic_Emu.cpp \
	$(GME)/Data_Reader.cpp \
	$(GME)/Dual_Resampler.cpp \
	$(GME)/Fir_Resampler.cpp \
	$(GME)/gme.cpp \
	$(GME)/Gme_File.cpp \
	$(GME)/M3u_Playlist.cpp \
	$(GME)/Multi_Buffer.cpp \
	$(GME)/Music_Emu.cpp \
	$(GME)/Ay_Apu.cpp \
	$(GME)/Ay_Cpu.cpp \
	$(GME)/Ay_Emu.cpp \
	$(GME)/Gb_Apu.cpp \
	$(GME)/Gb_Cpu.cpp \
	$(GME)/Gb_Oscs.cpp \
	$(GME)/Gbs_Emu.cpp \
	$(GME)/Gym_Emu.cpp \
	$(GME)/Hes_Apu.cpp \
	$(GME)/Hes_Apu_Adpcm.cpp \
	$(GME)/Hes_Cpu.cpp \
	$(GME)/Hes_Emu.cpp \
	$(GME)/Kss_Cpu.cpp \
	$(GME)/Kss_Emu.cpp \
	$(GME)/Kss_Scc_Apu.cpp \
	$(GME)/Nes_Apu.cpp \
	$(GME)/Nes_Cpu.cpp \
	$(GME)/Nes_Fme7_Apu.cpp \
	$(GME)/Nes_Namco_Apu.cpp \
	$(GME)/Nes_Oscs.cpp \
	$(GME)/Nes_Vrc6_Apu.cpp \
	$(GME)/Nes_Fds_Apu.cpp \
	$(GME)/Nes_Vrc7_Apu.cpp \
	$(GME)/Nsf_Emu.cpp \
	$(GME)/Nsfe_Emu.cpp \
	$(GME)/Snes_Spc.cpp \
	$(GME)/Spc_Cpu.cpp \
	$(GME)/Spc_Dsp.cpp \
	$(GME)/Spc_Emu.cpp \
	$(GME)/Spc_Filter.cpp \
	$(GME)/Vgm_Emu.cpp \
	$(GME)/Vgm_Emu_Impl.cpp \
	$(GME)/Ym2413_Emu.cpp \
	$(GME)/Ym2612_Nuked.cpp \
	$(GME)/Sms_Apu.cpp

GME_C_SRCS = $(GME)/ext/emu2413.c

NSF_SRCS = \
	$(NSF)/xgm/devices/Audio/MedianFilter.cpp \
	$(NSF)/xgm/devices/Audio/echo.cpp \
	$(NSF)/xgm/devices/Audio/filter.cpp \
	$(NSF)/xgm/devices/Audio/rconv.cpp \
	$(NSF)/xgm/devices/CPU/nes_cpu.cpp \
	$(NSF)/xgm/devices/Memory/nes_bank.cpp \
	$(NSF)/xgm/devices/Memory/nes_mem.cpp \
	$(NSF)/xgm/devices/Memory/nsf2_vectors.cpp \
	$(NSF)/xgm/devices/Memory/ram64k.cpp \
	$(NSF)/xgm/devices/Misc/detect.cpp \
	$(NSF)/xgm/devices/Misc/log_cpu.cpp \
	$(NSF)/xgm/devices/Misc/nes_detect.cpp \
	$(NSF)/xgm/devices/Misc/nsf2_irq.cpp \
	$(NSF)/xgm/devices/Sound/nes_apu.cpp \
	$(NSF)/xgm/devices/Sound/nes_dmc.cpp \
	$(NSF)/xgm/devices/Sound/nes_fds.cpp \
	$(NSF)/xgm/devices/Sound/nes_fme7.cpp \
	$(NSF)/xgm/devices/Sound/nes_mmc5.cpp \
	$(NSF)/xgm/devices/Sound/nes_n106.cpp \
	$(NSF)/xgm/devices/Sound/nes_vrc6.cpp \
	$(NSF)/xgm/devices/Sound/nes_vrc7.cpp \
	$(NSF)/xgm/player/nsf/nsf.cpp \
	$(NSF)/xgm/player/nsf/nsfconfig.cpp \
	$(NSF)/xgm/player/nsf/nsfplay.cpp \
	$(NSF)/xgm/player/nsf/pls/ppls.cpp \
	$(NSF)/xgm/player/nsf/pls/sstream.cpp \
	$(NSF)/xgm/fileutil.cpp \
	$(NSF)/vcm/group.cpp \
	$(NSF)/vcm/value.cpp

NSF_C_SRCS = \
	$(NSF)/xgm/devices/Sound/legacy/emu2149.c \
	$(NSF)/xgm/devices/Sound/legacy/emu2212.c \
	$(NSF)/xgm/devices/Sound/legacy/emu2413.c

FAT_SRCS = \
	$(FAT)/NSF_Core.cpp \
	$(FAT)/NSF_File.cpp \
	$(FAT)/NSF_6502.cpp \
	$(FAT)/Wave_VRC7.cpp

FAT_C_SRCS = $(FAT)/fmopl.c

LZ7_SRCS = \
	$(LZ7)/7zAlloc.c \
	$(LZ7)/7zArcIn.c \
	$(LZ7)/7zBuf.c \
	$(LZ7)/7zCrc.c \
	$(LZ7)/7zCrcOpt.c \
	$(LZ7)/7zDec.c \
	$(LZ7)/7zStream.c \
	$(LZ7)/Bcj2.c \
	$(LZ7)/Bra.c \
	$(LZ7)/Bra86.c \
	$(LZ7)/BraIA64.c \
	$(LZ7)/CpuArch.c \
	$(LZ7)/Delta.c \
	$(LZ7)/LzmaDec.c \
	$(LZ7)/Lzma2Dec.c

PSF_SRCS = $(PSF)/psflib.c

VIO_SRCS = \
	$(VIO)/vbam/apu/Multi_Buffer.cpp \
	$(VIO)/vbam/apu/Gb_Oscs.cpp \
	$(VIO)/vbam/apu/Gb_Apu.cpp \
	$(VIO)/vbam/apu/Effects_Buffer.cpp \
	$(VIO)/vbam/apu/Blip_Buffer.cpp \
	$(VIO)/vbam/gba/Sound.cpp \
	$(VIO)/vbam/gba/GBA.cpp \
	$(VIO)/vbam/gba/GBA-thumb.cpp \
	$(VIO)/vbam/gba/GBA-arm.cpp \
	$(VIO)/vbam/gba/bios.cpp

HA_SRCS = \
	$(HA)/VBA/bios.cpp \
	$(HA)/VBA/Sound.cpp \
	$(HA)/VBA/snd_interp.cpp \
	$(HA)/VBA/Globals.cpp \
	$(HA)/resample.cpp \
	$(HA)/resamplesubs.cpp \
	$(HA)/filterkit.cpp

HA_GBA_SRC = $(HA)/VBA/GBA.cpp
HA_ENG_SRCS = \
	$(SRC)/engines/ha_host.cpp \
	$(SRC)/engines/ha_engine.cpp

USF_ZLIB_SRCS = $(TP)/usf_zlib/adler32.c

USF_SRCS = \
	$(USF)/ai/ai_controller.c \
	$(USF)/api/callbacks.c \
	$(USF)/debugger/dbg_decoder.c \
	$(USF)/main/main.c \
	$(USF)/main/rom.c \
	$(USF)/main/savestates.c \
	$(USF)/main/util.c \
	$(USF)/memory/memory.c \
	$(USF)/pi/cart_rom.c \
	$(USF)/pi/pi_controller.c \
	$(USF)/r4300/cached_interp.c \
	$(USF)/r4300/cp0.c \
	$(USF)/r4300/cp1.c \
	$(USF)/r4300/empty_dynarec.c \
	$(USF)/r4300/exception.c \
	$(USF)/r4300/interupt.c \
	$(USF)/r4300/mi_controller.c \
	$(USF)/r4300/pure_interp.c \
	$(USF)/r4300/r4300.c \
	$(USF)/r4300/r4300_core.c \
	$(USF)/r4300/recomp.c \
	$(USF)/r4300/reset.c \
	$(USF)/r4300/tlb.c \
	$(USF)/rdp/rdp_core.c \
	$(USF)/ri/rdram.c \
	$(USF)/ri/rdram_detection_hack.c \
	$(USF)/ri/ri_controller.c \
	$(USF)/rsp/rsp_core.c \
	$(USF)/rsp_hle/alist.c \
	$(USF)/rsp_hle/alist_audio.c \
	$(USF)/rsp_hle/alist_naudio.c \
	$(USF)/rsp_hle/alist_nead.c \
	$(USF)/rsp_hle/audio.c \
	$(USF)/rsp_hle/cicx105.c \
	$(USF)/rsp_hle/hle.c \
	$(USF)/rsp_hle/hvqm.c \
	$(USF)/rsp_hle/jpeg.c \
	$(USF)/rsp_hle/memory.c \
	$(USF)/rsp_hle/mp3.c \
	$(USF)/rsp_hle/musyx.c \
	$(USF)/rsp_hle/plugin.c \
	$(USF)/rsp_hle/re2.c \
	$(USF)/rsp_lle/rsp.c \
	$(USF)/si/cic.c \
	$(USF)/si/game_controller.c \
	$(USF)/si/n64_cic_nus_6105.c \
	$(USF)/si/pif.c \
	$(USF)/si/si_controller.c \
	$(USF)/usf/barray.c \
	$(USF)/usf/resampler.c \
	$(USF)/usf/usf.c \
	$(USF)/vi/vi_controller.c

OUR_C = \
	$(SRC)/probe.c \
	$(SRC)/m3u.c \
	$(SRC)/archive.c \
	$(SRC)/config.c \
	$(SRC)/volume.c \
	$(SRC)/player.c \
	$(SRC)/measure.c \
	$(SRC)/lencache.c \
	$(SRC)/vfs.c \
	$(SRC)/psf_io.c \
	$(SRC)/engines/nez_engine.c \
	$(SRC)/sevenz.c

OUR_CXX = \
	$(SRC)/engines/gme_engine.cpp \
	$(SRC)/engines/nsfplay_engine.cpp \
	$(SRC)/engines/fatso_engine.cpp \
	$(SRC)/engines/gsf_engine.cpp \
	$(SRC)/engines/usf_engine.cpp

NEZ_L = $(patsubst $(NEZ)/%.c,$(OBJ)/nez/%.o,$(NEZ_SRCS))
GME_L = $(patsubst $(GME)/%.cpp,$(OBJ)/gme/%.o,$(GME_SRCS))
GMEC_L = $(patsubst $(GME)/%.c,$(OBJ)/gme/%.o,$(GME_C_SRCS))
NSF_L = $(patsubst $(NSF)/%.cpp,$(OBJ)/nsf/%.o,$(NSF_SRCS))
NSFC_L = $(patsubst $(NSF)/%.c,$(OBJ)/nsf/%.o,$(NSF_C_SRCS))
FAT_L = $(patsubst $(FAT)/%.cpp,$(OBJ)/fat/%.o,$(FAT_SRCS))
FATC_L = $(patsubst $(FAT)/%.c,$(OBJ)/fat/%.o,$(FAT_C_SRCS))
OURC_L = $(patsubst $(SRC)/%.c,$(OBJ)/src/%.o,$(OUR_C))
OURX_L = $(patsubst $(SRC)/%.cpp,$(OBJ)/src/%.o,$(OUR_CXX))
LZ7_L = $(patsubst $(LZ7)/%.c,$(OBJ)/lz7/%.o,$(LZ7_SRCS))
PSF_L = $(patsubst $(PSF)/%.c,$(OBJ)/psf/%.o,$(PSF_SRCS))
VIO_L = $(patsubst $(VIO)/%.cpp,$(OBJ)/vio/%.o,$(VIO_SRCS))
USF_L = $(patsubst $(USF)/%.c,$(OBJ)/usf/%.o,$(USF_SRCS))
HA_L = $(patsubst $(HA)/%.cpp,$(OBJ)/ha/%.o,$(HA_SRCS))
HA_GBA_L = $(OBJ)/ha/VBA/GBA.o
HAENG_L = $(patsubst $(SRC)/engines/%.cpp,$(OBJ)/haeng/%.o,$(HA_ENG_SRCS))

NEZ_W = $(patsubst $(NEZ)/%.c,$(OBJW)/nez/%.o,$(NEZ_SRCS))
GME_W = $(patsubst $(GME)/%.cpp,$(OBJW)/gme/%.o,$(GME_SRCS))
GMEC_W = $(patsubst $(GME)/%.c,$(OBJW)/gme/%.o,$(GME_C_SRCS))
NSF_W = $(patsubst $(NSF)/%.cpp,$(OBJW)/nsf/%.o,$(NSF_SRCS))
NSFC_W = $(patsubst $(NSF)/%.c,$(OBJW)/nsf/%.o,$(NSF_C_SRCS))
FAT_W = $(patsubst $(FAT)/%.cpp,$(OBJW)/fat/%.o,$(FAT_SRCS))
FATC_W = $(patsubst $(FAT)/%.c,$(OBJW)/fat/%.o,$(FAT_C_SRCS))
OURC_W = $(patsubst $(SRC)/%.c,$(OBJW)/src/%.o,$(OUR_C))
OURX_W = $(patsubst $(SRC)/%.cpp,$(OBJW)/src/%.o,$(OUR_CXX))
LZ7_W = $(patsubst $(LZ7)/%.c,$(OBJW)/lz7/%.o,$(LZ7_SRCS))
PSF_W = $(patsubst $(PSF)/%.c,$(OBJW)/psf/%.o,$(PSF_SRCS))
VIO_W = $(patsubst $(VIO)/%.cpp,$(OBJW)/vio/%.o,$(VIO_SRCS))
USF_W = $(patsubst $(USF)/%.c,$(OBJW)/usf/%.o,$(USF_SRCS))
USFZ_W = $(patsubst $(TP)/%.c,$(OBJW)/usfz/%.o,$(USF_ZLIB_SRCS))
HA_W = $(patsubst $(HA)/%.cpp,$(OBJW)/ha/%.o,$(HA_SRCS))
HA_GBA_W = $(OBJW)/ha/VBA/GBA.o
HAENG_W = $(patsubst $(SRC)/engines/%.cpp,$(OBJW)/haeng/%.o,$(HA_ENG_SRCS))

# Fatso is MSVC-era and needs a real Windows.h; host tests fall back without it.
HOST_LIBS = $(NEZ_L) $(GME_L) $(GMEC_L) $(NSF_L) $(NSFC_L) $(LZ7_L) $(PSF_L) $(VIO_L) $(USF_L) $(HA_L) $(HA_GBA_L) $(HAENG_L) $(OURC_L) $(OURX_L)
WIN_LIBS  = $(NEZ_W) $(GME_W) $(GMEC_W) $(NSF_W) $(NSFC_W) $(FAT_W) $(FATC_W) $(LZ7_W) $(PSF_W) $(VIO_W) $(USF_W) $(USFZ_W) $(HA_W) $(HA_GBA_W) $(HAENG_W) $(OURC_W) $(OURX_W)

.PHONY: all dll test pack clean

all: test dll

dll: $(DIST)/xmp-gamemusic.dll

test: $(DIST)/test_render
	$(DIST)/test_render

$(OBJ)/nez/%.o: $(NEZ)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COM) -fPIC -c -o $@ $<

$(OBJ)/gme/%.o: $(GME)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_GME) -fPIC -c -o $@ $<

$(OBJ)/gme/%.o: $(GME)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COM) -fPIC -c -o $@ $<

$(OBJ)/nsf/%.o: $(NSF)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_NSF) -fPIC -c -o $@ $<

$(OBJ)/nsf/%.o: $(NSF)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_NSF) -fPIC -c -o $@ $<

$(OBJ)/fat/%.o: $(FAT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_FAT) -fPIC -I$(FAT)/compat -c -o $@ $<

$(OBJ)/fat/%.o: $(FAT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_FAT) -fPIC -c -o $@ $<

$(OBJ)/lz7/%.o: $(LZ7)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COM) -fPIC -c -o $@ $<

$(OBJ)/psf/%.o: $(PSF)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COM) -fPIC -I$(TP)/miniz_zlib -c -o $@ $<

$(OBJ)/vio/%.o: $(VIO)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS_COM) -std=c++14 -fPIC -c -o $@ $<

$(OBJ)/usf/%.o: $(USF)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COM) -fPIC -I$(USF) -c -o $@ $<

$(OBJ)/ha/%.o: $(HA)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_HA) -fPIC -c -o $@ $<

$(OBJ)/ha/VBA/GBA.o: $(HA)/VBA/GBA.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_HA) -O1 -fPIC -c -o $@ $<

$(OBJ)/haeng/%.o: $(SRC)/engines/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_HA) -fPIC -c -o $@ $<

$(OBJ)/src/%.o: $(SRC)/%.c $(SRC)/gamechip.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COM) -fPIC -c -o $@ $<

$(OBJ)/src/%.o: $(SRC)/%.cpp $(SRC)/gamechip.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_PLUG) -fPIC -c -o $@ $<

$(OBJW)/nez/%.o: $(NEZ)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/gme/%.o: $(GME)/%.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_GME) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/gme/%.o: $(GME)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/nsf/%.o: $(NSF)/%.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_NSF) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/nsf/%.o: $(NSF)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_NSF) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/fat/%.o: $(FAT)/%.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_FAT) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/fat/%.o: $(FAT)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_FAT) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/lz7/%.o: $(LZ7)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/psf/%.o: $(PSF)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -I$(TP)/miniz_zlib -c -o $@ $<

$(OBJW)/vio/%.o: $(VIO)/%.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CFLAGS_COM) -std=c++14 -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/usf/%.o: $(USF)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -I$(USF) -I$(TP)/usf_zlib -c -o $@ $<

$(OBJW)/usfz/%.o: $(TP)/%.c
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/ha/%.o: $(HA)/%.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_HA) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/ha/VBA/GBA.o: $(HA)/VBA/GBA.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_HA) -O1 -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/haeng/%.o: $(SRC)/engines/%.cpp
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_HA) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/src/%.o: $(SRC)/%.c $(SRC)/gamechip.h
	@mkdir -p $(dir $@)
	$(I686_CC) $(CFLAGS_COM) -DWIN32 -D_WIN32 -c -o $@ $<

$(OBJW)/src/%.o: $(SRC)/%.cpp $(SRC)/gamechip.h
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS_PLUG) -DWIN32 -D_WIN32 -c -o $@ $<

$(DIST)/test_render: $(ROOT)/tests/test_render.c $(HOST_LIBS)
	mkdir -p $(DIST)
	$(CXX) $(CXXFLAGS_PLUG) -fPIC -o $@ $(ROOT)/tests/test_render.c $(HOST_LIBS) -lm -lpthread -lz

$(OBJW)/xmp-gamemusic.res: $(SRC)/xmp-gamemusic.rc
	@mkdir -p $(dir $@)
	$(I686_WINDRES) -O coff -I$(SRC) -o $@ $<

$(DIST)/xmp-gamemusic.dll: $(SRC)/xmp-gamemusic.cpp $(SRC)/xmp-gamemusic.def $(WIN_LIBS) $(OBJW)/xmp-gamemusic.res
	mkdir -p $(DIST)
	$(I686_CXX) -shared -O2 -DNDEBUG -std=c++14 \
	  -static -static-libgcc -static-libstdc++ \
	  -I$(INC) $(INCS_COM) -DWIN32 -D_WIN32 \
	  -o $@ $(SRC)/xmp-gamemusic.cpp $(SRC)/xmp-gamemusic.def \
	  $(WIN_LIBS) $(OBJW)/xmp-gamemusic.res \
	  -Wl,--kill-at -Wl,--add-stdcall-alias \
	  -luser32 -lgdi32 -lcomctl32 -lkernel32 -Wl,-s
	$(I686_HOST)-objdump -p $@ | grep -E 'dll name|XMPIN_GetInterface|file format' || true
	-file $@ 2>/dev/null || true

pack: dll
	rm -f /workspace/xmp-gamemusic-1.0.11.zip
	mkdir -p $(DIST)/pack
	cp -f $(DIST)/xmp-gamemusic.dll $(ROOT)/README.md $(DIST)/pack/
	cd $(DIST)/pack && (command -v zip >/dev/null && zip -9 /workspace/xmp-gamemusic-1.0.11.zip xmp-gamemusic.dll README.md || python3 -c "import zipfile; z=zipfile.ZipFile('/workspace/xmp-gamemusic-1.0.11.zip','w',zipfile.ZIP_DEFLATED); z.write('xmp-gamemusic.dll'); z.write('README.md'); z.close()")
	rm -rf $(DIST)/pack
	ls -l /workspace/xmp-gamemusic-1.0.11.zip
	sha256sum /workspace/xmp-gamemusic-1.0.11.zip

clean:
	rm -rf $(DIST)
