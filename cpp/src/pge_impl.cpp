// The single translation unit where olcPixelGameEngine3's implementation is
// compiled in, per its single-header-library convention (see the "Single
// olcPixelGameEngine implementation TU rule" in the plan's Global
// Constraints). Every other file includes olcPixelGameEngine3.h for
// declarations only — do not add OLC_PGE3_APPLICATION anywhere else.
#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"
