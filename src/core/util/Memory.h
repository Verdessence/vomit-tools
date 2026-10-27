//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Giving freed memory back to the system. glibc's malloc keeps what a program frees for
//          its next allocations, and only returns it when asked; after something big is gone (a
//          map closed, a load's scratch freed) asking keeps the process's footprint honest.
//
//===========================================================================//
#pragma once

namespace xv
{

// Returns the heap's free pages to the system where the C library can (glibc); nothing elsewhere.
void ReleaseFreedMemory();
// Called first thing in main: fixes glibc's thresholds for handing memory back, which it otherwise
// raises as the program frees big blocks, until each thread's heap may keep up to 64 MB it no
// longer uses (a dozen worker threads keep hundreds after a big map is closed).
void TuneAllocator();

} // namespace xv
