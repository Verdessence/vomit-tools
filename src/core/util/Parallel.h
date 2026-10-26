//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Runs a loop's body on the machine's cores. Threads take the next slice from a shared
//          counter, so slices that cost more (a big solid, a busy row) don't hold the others up.
//
//===========================================================================//
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace xv
{

// How many threads ParallelFor uses: the cores, within reason. (std::thread rather than jthread:
// libc++ only has jthread from version 18.)
[[nodiscard]] inline std::size_t ThreadCount()
{
	return std::clamp<std::size_t>( std::thread::hardware_concurrency(), 1, 32 );
}

// Calls body(begin, end) on slices of [0, count) about grain long, in parallel. Small loops run
// on the calling thread.
template <typename Body>
void ParallelFor( std::size_t nCount, std::size_t nGrain, Body &&body )
{
	nGrain = std::max<std::size_t>( nGrain, 1 );
	const std::size_t nThreads = std::min( ThreadCount(), ( nCount + nGrain - 1 ) / nGrain );
	if ( nThreads <= 1 )
	{
		if ( nCount > 0 )
		{
			body( std::size_t{ 0 }, nCount );
		}
		return;
	}
	std::atomic<std::size_t> nNext{ 0 };
	auto work = [ & ] {
		for ( ;; )
		{
			const std::size_t nBegin = nNext.fetch_add( nGrain );
			if ( nBegin >= nCount )
			{
				return;
			}
			body( nBegin, std::min( nCount, nBegin + nGrain ) );
		}
	};
	std::vector<std::thread> workers;
	workers.reserve( nThreads - 1 );
	for ( std::size_t i = 1; i < nThreads; i++ )
	{
		workers.emplace_back( work );
	}
	work();
	for ( std::thread &worker : workers )
	{
		worker.join();
	}
}

} // namespace xv
