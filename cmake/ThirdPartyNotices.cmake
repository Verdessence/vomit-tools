# Writes THIRD-PARTY-NOTICES.txt into the build folder: every library built into vomit-tools or
# shipped with it, where its source is (the exact commit fetched, when it was fetched from git),
# and its license text as that source has it. The app shows the file under Help > About >
# Licenses and the packages carry it beside LICENSE, so the notices always match what was built.
#
# Libraries fetched by sourcepp and minizip-ng are found where FetchContent put them.

set(VOMIT_TOOLS_NOTICES "${CMAKE_BINARY_DIR}/THIRD-PARTY-NOTICES.txt")

# The folder a fetched library is in: FetchContent's record, else the usual places.
function(vomit_tools_notice_source NAME OUT)
    FetchContent_GetProperties(${NAME} SOURCE_DIR dir)
    if(NOT dir OR NOT EXISTS "${dir}")
        foreach(candidate "${CMAKE_BINARY_DIR}/_deps/${NAME}-src" "${CMAKE_BINARY_DIR}/_deps/minizip-ng-build/_deps/${NAME}-src")
            if(EXISTS "${candidate}")
                set(dir "${candidate}")
                break()
            endif()
        endforeach()
    endif()
    set(${OUT} "${dir}" PARENT_SCOPE)
endfunction()

set(notices "Third-party software in vomit-tools
===================================

vomit-tools is free software under the GNU General Public License, version 3 or later (the
LICENSE file beside this one, or Help > About > Licenses). Its source is at
https://github.com/Verdessence/vomit-tools; the source of a release is the tag of that version.

The libraries below are built into vomit-tools or shipped with it. Each comes with where its
source is and its license, exactly as its source has it. Where a library was fetched from git,
the commit given is the one this build used.
")

# NAME: shown name; FETCH: FetchContent name (empty when the source isn't fetched); FILE: the
# license file in the source (or an absolute path; none when NOTE says it all); URL: where the
# source is; LICENSE: SPDX id.
function(vomit_tools_add_notice)
    cmake_parse_arguments(ARG "" "NAME;FETCH;FILE;URL;LICENSE;NOTE" "" ${ARGN})
    set(source "")
    set(file "${ARG_FILE}")
    if(ARG_FETCH)
        vomit_tools_notice_source(${ARG_FETCH} source)
        set(file "${source}/${ARG_FILE}")
    endif()
    set(where "${ARG_URL}")
    if(source AND EXISTS "${source}/.git")
        execute_process(COMMAND git -C "${source}" rev-parse HEAD
                OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
        if(NOT failed AND commit)
            string(APPEND where " (commit ${commit})")
        endif()
    endif()
    set(entry "\n\n------------------------------------------------------------------------------\n${ARG_NAME}\n")
    string(APPEND entry "Source: ${where}\nLicense: ${ARG_LICENSE}\n")
    if(ARG_NOTE)
        string(APPEND entry "${ARG_NOTE}\n")
    endif()
    string(APPEND entry "------------------------------------------------------------------------------\n\n")
    if(NOT ARG_FILE)
        # The note says it all.
    elseif(EXISTS "${file}" AND NOT IS_DIRECTORY "${file}")
        file(READ "${file}" text)
        string(APPEND entry "${text}")
    else()
        message(WARNING "No license text for ${ARG_NAME} at ${file}; THIRD-PARTY-NOTICES.txt is incomplete")
        string(APPEND entry "(The license text wasn't found when this build was configured.)\n")
    endif()
    set(notices "${notices}${entry}" PARENT_SCOPE)
endfunction()

vomit_tools_add_notice(NAME "Qt" FILE "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-3.0-only.txt"
        URL "https://download.qt.io/official_releases/qt/ (version ${Qt6_VERSION})" LICENSE "LGPL-3.0-only"
        NOTE "Qt's libraries are linked dynamically and shipped as separate files, so they can be replaced
with other builds of the same version. Qt itself contains third-party code; its notices are listed
at https://doc.qt.io/qt-6/licenses-used-in-qt.html and in Help > About Qt.")
vomit_tools_add_notice(NAME "sourcepp (vpkpp, kvpp and their support code)" FETCH sourcepp FILE LICENSE
        URL "https://github.com/craftablescience/sourcepp/archive/5dcbbe8ac556b1c833cccf31a2b016e114dbc3f0.tar.gz" LICENSE "MIT")
vomit_tools_add_notice(NAME "BufferStream" FETCH bufferstream FILE LICENSE
        URL "https://github.com/craftablescience/BufferStream" LICENSE "MIT")
vomit_tools_add_notice(NAME "half" FETCH sourcepp FILE ext/half/LICENSE.txt
        URL "https://half.sourceforge.net (in sourcepp's ext/half)" LICENSE "MIT")
vomit_tools_add_notice(NAME "hat-trie" FETCH tsl_hat_trie FILE LICENSE
        URL "https://github.com/Tessil/hat-trie" LICENSE "MIT")
vomit_tools_add_notice(NAME "libtomcrypt" FETCH libtomcrypt FILE LICENSE
        URL "https://github.com/libtom/libtomcrypt" LICENSE "Unlicense")
vomit_tools_add_notice(NAME "libtommath" FETCH libtommath FILE LICENSE
        URL "https://github.com/libtom/libtommath" LICENSE "Unlicense")
vomit_tools_add_notice(NAME "miniz" FETCH miniz FILE LICENSE
        URL "https://github.com/richgel999/miniz" LICENSE "MIT")
vomit_tools_add_notice(NAME "minizip-ng" FETCH minizip-ng FILE LICENSE
        URL "https://github.com/zlib-ng/minizip-ng" LICENSE "Zlib")
vomit_tools_add_notice(NAME "zlib-ng" FETCH zlib FILE LICENSE.md
        URL "https://github.com/zlib-ng/zlib-ng" LICENSE "Zlib")
vomit_tools_add_notice(NAME "Zstandard" FETCH zstd FILE LICENSE
        URL "https://github.com/facebook/zstd" LICENSE "BSD-3-Clause (chosen of BSD-3-Clause OR GPL-2.0-only)")
vomit_tools_add_notice(NAME "bzip2" FETCH bzip2 FILE LICENSE
        URL "https://sourceware.org/git/bzip2.git" LICENSE "bzip2-1.0.6")
vomit_tools_add_notice(NAME "liblzma (XZ Utils)" FETCH liblzma FILE COPYING.0BSD
        URL "https://github.com/tukaani-project/xz" LICENSE "0BSD")
vomit_tools_add_notice(NAME "PPMd (from 7-Zip)" FETCH ppmd
        URL "https://github.com/ip7z/7zip" LICENSE "Public domain"
        NOTE "Only 7-Zip's PPMd codec files are used. Their headers read, for example (C/Ppmd7.c):
  2023-09-07 : Igor Pavlov : Public domain
  This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain")
vomit_tools_add_notice(NAME "miniaudio" FETCH miniaudio FILE LICENSE
        URL "https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz" LICENSE "Unlicense OR MIT-0")
vomit_tools_add_notice(NAME "stb" FETCH stb FILE LICENSE
        URL "https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz" LICENSE "MIT OR Unlicense")

file(WRITE "${VOMIT_TOOLS_NOTICES}.new" "${notices}")
file(COPY_FILE "${VOMIT_TOOLS_NOTICES}.new" "${VOMIT_TOOLS_NOTICES}" ONLY_IF_DIFFERENT)
file(REMOVE "${VOMIT_TOOLS_NOTICES}.new")
