# Options for vomit-tools's own targets; included after the dependencies so they don't leak into them.
if(MSVC)
    # Sources are UTF-8; report the real __cplusplus; standard-conforming name lookup.
    add_compile_options(/utf-8 /Zc:__cplusplus /permissive-)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

function(vomit_tools_set_warnings TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4)
    else()
        target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
