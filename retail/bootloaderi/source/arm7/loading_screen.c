#include "common.h"
#include "loading_screen.h"
#include "np3d_trace.h"
#include <nds/system.h>

void clearScreen(void) {

	// Wait until the ARM9 is ready
	while (arm9_stateFlag != ARM9_READY);

	arm9_stateFlag = ARM9_SCRNCLR;

	// Wait until the ARM9 is ready
	while (arm9_stateFlag != ARM9_READY);
}

void esrbOutput(void) {
	np3dTraceEvent("rating display: waiting for ARM9", arm9_stateFlag, 0, 0);
	// Wait until the ARM9 is ready
	while (arm9_stateFlag != ARM9_READY);

	arm9_stateFlag = ARM9_DISPESRB;
	np3dTraceEvent("rating display requested", arm9_stateFlag, 0, 0);

	// Wait until the ARM9 is ready
	//while (arm9_stateFlag != ARM9_READY);
}

void pleaseWaitOutput(void) {
	np3dTraceEvent("loading text: waiting for ARM9", arm9_stateFlag, 0, 0);
	// Wait until the ARM9 is ready
	while (arm9_stateFlag != ARM9_READY);

	arm9_stateFlag = ARM9_DISPSCRN;
	np3dTraceEvent("loading text requested", arm9_stateFlag, 0, 0);

	// Wait until the ARM9 is ready
	while (arm9_stateFlag != ARM9_READY);
}

void errorOutput(void) {
	np3dTraceEvent("ERROR loader (a=caller b=ARM9 state c=SCFG)", (u32)__builtin_return_address(0), arm9_stateFlag, REG_SCFG_EXT);
	// Wait until the ARM9 is ready
	while (arm9_stateFlag != ARM9_READY);

	arm9_stateFlag = ARM9_DISPERR;

	// Stop
	while (1);
}
