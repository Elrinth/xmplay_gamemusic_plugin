/* Prefix NSFPlay's emu2413 so it does not collide with libgme's copy. */
#ifndef NSFPLAY_EMU2413_RENAME_H
#define NSFPLAY_EMU2413_RENAME_H
#define OPLL_RateConv_new nsf_OPLL_RateConv_new
#define OPLL_RateConv_reset nsf_OPLL_RateConv_reset
#define OPLL_RateConv_putData nsf_OPLL_RateConv_putData
#define OPLL_RateConv_getData nsf_OPLL_RateConv_getData
#define OPLL_RateConv_delete nsf_OPLL_RateConv_delete
#define OPLL_new nsf_OPLL_new
#define OPLL_delete nsf_OPLL_delete
#define OPLL_reset nsf_OPLL_reset
#define OPLL_forceRefresh nsf_OPLL_forceRefresh
#define OPLL_setRate nsf_OPLL_setRate
#define OPLL_setQuality nsf_OPLL_setQuality
#define OPLL_setChipType nsf_OPLL_setChipType
#define OPLL_writeReg nsf_OPLL_writeReg
#define OPLL_writeIO nsf_OPLL_writeIO
#define OPLL_setPan nsf_OPLL_setPan
#define OPLL_setPanFine nsf_OPLL_setPanFine
#define OPLL_dumpToPatch nsf_OPLL_dumpToPatch
#define OPLL_getDefaultPatch nsf_OPLL_getDefaultPatch
#define OPLL_setPatch nsf_OPLL_setPatch
#define OPLL_patchToDump nsf_OPLL_patchToDump
#define OPLL_copyPatch nsf_OPLL_copyPatch
#define OPLL_resetPatch nsf_OPLL_resetPatch
#define OPLL_calc nsf_OPLL_calc
#define OPLL_calcStereo nsf_OPLL_calcStereo
#define OPLL_setMask nsf_OPLL_setMask
#define OPLL_toggleMask nsf_OPLL_toggleMask
#endif
