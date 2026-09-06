// The single translation unit where olcPixelGameEngine's implementation is
// compiled in, per its single-header-library convention (see the "Single
// olcPixelGameEngine implementation TU rule" in the plan's Global
// Constraints). Every other file includes olcPixelGameEngine.h for
// declarations only — do not add OLC_PGE_APPLICATION anywhere else.
#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
