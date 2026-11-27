# The harness drives the file a saved game is kept in: savefile.cpp's writer and reader over
# the field table, the compressed content, and every refusal the reader makes. It links the
# same vendored LZO library the engine does, and compiles crc.cpp for the checksum the
# header and the content carry.

opents_add_test(SaveTest
    NAME save
    SOURCES savetest.cpp
    ENGINE
        crc.cpp
        savefile.cpp
    DEFINITIONS NOMINMAX
    LIBRARIES lzo
    DOCTEST
)

if(OPENTS_POSIX)
    target_sources(SaveTest PRIVATE "${CMAKE_SOURCE_DIR}/code/platform/posix/wincompat.cpp")
    target_include_directories(SaveTest PRIVATE "${CMAKE_SOURCE_DIR}/code/platform/posix")
else()
    target_compile_definitions(SaveTest PRIVATE WIN32 _WINDOWS _MBCS)
endif()
