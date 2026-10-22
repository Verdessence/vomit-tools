// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What this build is. The definitions are generated at build time (BuildInfo.cpp.in).
namespace buildinfo {

extern const char* const version; // the project version, "0.3.0"
extern const char* const commit;  // `git describe` of the sources ("v0.3.0-2-g1a2b3c4-dirty"), or "" outside git

} // namespace buildinfo
