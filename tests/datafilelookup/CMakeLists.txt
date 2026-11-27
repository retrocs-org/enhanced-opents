opents_add_test(DataFileLookup
    NAME datafilelookup
    SOURCES datafilelookup.cpp
    ENGINE
        cdfile.cpp
        bfiofile.cpp
        rawfile.cpp
        file.cpp
        file_posix.cpp
    INCLUDES "${CMAKE_SOURCE_DIR}/code/platform/posix"
    DOCTEST
)
target_compile_options(DataFileLookup PRIVATE
    "-include" "${CMAKE_SOURCE_DIR}/code/platform/posix/wincompat.h"
)
