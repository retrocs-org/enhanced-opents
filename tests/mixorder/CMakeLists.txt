# The mixfile search is compiled straight into the harness, along with the file classes it
# reads through. It lives outside code/ so that the recursive glob building OpenTS cannot pick
# this target's entry point up.

opents_add_test(MixOrder
    NAME mixorder
    SOURCES mixorder.cpp
    ENGINE
        base64.cpp
        mixfile.cpp
        ccfile.cpp
        cdfile.cpp
        bfiofile.cpp
        rawfile.cpp
        file.cpp
        file_posix.cpp
        file_win.cpp
        buff.cpp
        crc.cpp
        cstraw.cpp
        int.cpp
        mpmath.cpp
        pipe.cpp
        pk.cpp
        pkstraw.cpp
        blwstraw.cpp
        blowfish.cpp
        rndstraw.cpp
        shastraw.cpp
        sha.cpp
        straw.cpp
        xpipe.cpp
        xstraw.cpp
    INCLUDES ${OPENTS_GENERATED_DIR}
    DEFINITIONS NOMINMAX
    UTF8
    DOCTEST
)

if(OPENTS_POSIX)
    target_include_directories(MixOrder PRIVATE "${CMAKE_SOURCE_DIR}/code/platform/posix")
    target_compile_options(MixOrder PRIVATE "-include" "${CMAKE_SOURCE_DIR}/code/platform/posix/wincompat.h")
    target_sources(MixOrder PRIVATE "${CMAKE_SOURCE_DIR}/code/platform/posix/wincompat.cpp")
else()
    target_compile_definitions(MixOrder PRIVATE WIN32 _WINDOWS _MBCS)
    target_sources(MixOrder PRIVATE "${CMAKE_SOURCE_DIR}/code/dbgprint.cpp")
endif()
