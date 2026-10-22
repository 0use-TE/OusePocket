#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"
#include "i_system.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));
	if (!DG_ScreenBuffer) I_Error("No PSRAM for Doom screen buffer");

	DG_Init();

	D_DoomMain ();
}

