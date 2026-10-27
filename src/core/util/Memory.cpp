//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Memory.h.
//
//===========================================================================//
#include "Memory.h"

#include <cstdlib> // defines __GLIBC__ where it applies

#if defined( __GLIBC__ )
#include <malloc.h>
#endif

namespace xv
{

//-----------------------------------------------------------------------------
// Purpose: malloc_trim goes through every arena (each thread's too) and hands back the pages
//          that hold nothing, not only the top of the heap.
//-----------------------------------------------------------------------------
void ReleaseFreedMemory()
{
#if defined( __GLIBC__ )
	malloc_trim( 0 );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Blocks from 4 MB come straight from the system and go straight back; a heap gives back
//          its free top once it is past 4 MB. Setting either stops glibc moving both.
//-----------------------------------------------------------------------------
void TuneAllocator()
{
#if defined( __GLIBC__ )
	constexpr int THRESHOLD = 4 * 1024 * 1024;
	mallopt( M_MMAP_THRESHOLD, THRESHOLD );
	mallopt( M_TRIM_THRESHOLD, THRESHOLD );
#endif
}

} // namespace xv
