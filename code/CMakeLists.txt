# Limit the generated solution to supported configurations.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

#
# ---------------------------------------------------------
# Directories that build as their own project
# ---------------------------------------------------------
#
set(SUBPROJECT_DIRS
    "language"
    "vqalib"
)

#
# ---------------------------------------------------------
# Gather all potential OpenTS files
# ---------------------------------------------------------
#
file(GLOB_RECURSE OPENTS_RAW CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)

#
# ---------------------------------------------------------
# Remove files that belong to a subproject
# ---------------------------------------------------------
#
set(OPENTS_SRC "")
foreach(f ${OPENTS_RAW})
    set(EXCLUDED FALSE)
    foreach(dir ${SUBPROJECT_DIRS})
        if(f MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/${dir}/.*")
            set(EXCLUDED TRUE)
        endif()
    endforeach()

    # Resource scripts have no compiler off Windows; the POSIX port carries their content in
    # platform/posix instead, and the files there are left out of the Windows build.
    if(OPENTS_POSIX AND f MATCHES "\\.rc$")
        set(EXCLUDED TRUE)
    endif()
    if(NOT OPENTS_POSIX AND f MATCHES "/platform/posix/")
        set(EXCLUDED TRUE)
    endif()

    if(NOT EXCLUDED)
        list(APPEND OPENTS_SRC "${f}")
    endif()
endforeach()

#
# ---------------------------------------------------------
# Create actual OpenTS executable (Win32)
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating '${PROJECT_NAME}' executable...")

if(APPLE)
    set(OPENTS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/platform/posix/macos/OpenTS.icns")
    if(EXISTS "${OPENTS_ICON}")
        set_source_files_properties("${OPENTS_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    else()
        set(OPENTS_ICON "")
    endif()
    add_library(OpenTSMacDialogs OBJECT
        platform/posix/macos/gamedatapicker.mm
        platform/posix/macos/precisescroll.mm)
    target_link_libraries(OpenTSMacDialogs PRIVATE SDL3::SDL3-static)
    set_target_properties(OpenTSMacDialogs PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    set(OPENTS_ISO_IMPORTER "${CMAKE_SOURCE_DIR}/tools/macos/import-iso.sh")
    set_source_files_properties("${OPENTS_ISO_IMPORTER}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(OpenTS MACOSX_BUNDLE ${OPENTS_SRC} ${OPENTS_ICON} "${OPENTS_ISO_IMPORTER}")
    target_sources(OpenTS PRIVATE $<TARGET_OBJECTS:OpenTSMacDialogs>)
elseif(OPENTS_POSIX)
    add_executable(OpenTS ${OPENTS_SRC})
else()
    add_executable(OpenTS WIN32 ${OPENTS_SRC})
endif()
target_compile_features(OpenTS PRIVATE cxx_std_20)

#
# ---------------------------------------------------------
# Output paths
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting binary output paths...")

set_target_properties(OpenTS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"

    # Built under the shipped names, so the executable, the symbol file the debug record
    # names, and the map reach the run directory without a rename.
    OUTPUT_NAME_DEBUG "GameD"
    OUTPUT_NAME_RELEASE "Game"
    PDB_NAME_DEBUG "GameD"
    PDB_NAME_RELEASE "Game"
)

set_target_properties(OpenTS PROPERTIES
    VS_DEBUGGER_COMMAND "$<TARGET_FILE:OpenTS>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:OpenTS>"
    VS_DEBUGGER_COMMAND_ARGUMENTS "-DATADIR=${OPENTS_GAME_DIR}"
)

#
# ---------------------------------------------------------
# Compile flags
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying compiler flags...")

set(OPENTS_COMPILE_OPTIONS

    # ================= DEBUG =================
    $<$<CONFIG:Debug>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        # /utf-8 -- sources and literals are UTF-8 whatever the build machine's code page.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /Od /RTC1 /GR $<$<CXX_COMPILER_ID:MSVC>:/MP> /EHsc /Oy- /MTd /Zc:__cplusplus /arch:SSE2 /fp:precise /utf-8
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /Od /RTC1 $<$<C_COMPILER_ID:MSVC>:/MP> /Oy- /arch:SSE2 /fp:precise /utf-8
        >
    >

    # ================= RELEASE =================
    $<$<CONFIG:Release>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        # /utf-8 -- sources and literals are UTF-8 whatever the build machine's code page.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /O2 /GF /GR $<$<CXX_COMPILER_ID:MSVC>:/MP> /EHsc /MT /Zc:__cplusplus /arch:SSE2 /fp:precise /utf-8
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /O2 /GF $<$<C_COMPILER_ID:MSVC>:/MP> /arch:SSE2 /fp:precise /utf-8
        >
    >
)

if(OPENTS_POSIX)
    # The engine was written for MSVC on x86: signed char, wrapping signed arithmetic, no
    # strict aliasing, and IEEE single precision without contraction into fused operations.
    # platform/posix/wincompat.h stands in for the Windows headers and is forced into every
    # translation unit so the Win32 types and helpers resolve before any engine header.
    set(OPENTS_COMPILE_OPTIONS
        -fsigned-char
        -fwrapv
        -fno-strict-aliasing
        -ffp-contract=off
        -fms-extensions
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/platform/posix/wincompat.h"
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O2 -g>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
        -w
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND OPENTS_COMPILE_OPTIONS
            -Wno-microsoft
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>
            $<$<COMPILE_LANGUAGE:CXX>:-fdelayed-template-parsing>
        )
    else()
        list(APPEND OPENTS_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
    endif()
endif()

target_compile_options(OpenTS PRIVATE ${OPENTS_COMPILE_OPTIONS})

#
# ---------------------------------------------------------
# Subprojects
# ---------------------------------------------------------
#
# The VQA player carries its own project, and hands OpenTS its include path along with the
# library, so the movie code is reached the same way from anywhere in the solution.
add_subdirectory(vqalib)

message(STATUS "${PROJECT_NAME}: Adding include paths...")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OPENTS_GENERATED_DIR}"
)

# The generated build stamp has to exist before anything compiles.
add_dependencies(OpenTS OpenTSBuildStamp)

# bgfx's include paths and settings apply only to the two files that include it: the frame
# renderer and the UI overlay renderer.
set(BGFX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx")
set(OPENTS_BGFX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/rml/rmlrender.cpp"
)
set_source_files_properties(${OPENTS_BGFX_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES
        "${BGFX_ROOT}/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bx/include"
    COMPILE_DEFINITIONS
        "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,$<BOOL:${BX_CONFIG_DEBUG}>>"
    COMPILE_OPTIONS "$<$<BOOL:${MSVC}>:/Zc:preprocessor>"
)

# The frame quad's shaders come from the imgui example and the overlay's from the debug
# draw example. The imgui example directory also holds an imgui.h that would shadow Dear
# ImGui's, so only the file that needs those shaders gets that path.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${BGFX_ROOT}/examples/common/imgui"
)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui/rml/rmlrender.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${BGFX_ROOT}/examples/common/debugdraw"
)

# These files take miniz from bimg, which the link already carries.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/writepng.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deflate.cpp"
    PROPERTIES
    INCLUDE_DIRECTORIES
        "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bimg/3rdparty/tinyexr/deps"
)

# bx rewrites __stdcall while its headers are being parsed by clang-cl.  Force the
# compatibility header into the renderer translation units as well as bx/bgfx so the
# MSVC standard-library headers that follow still see the Win32 calling convention.
if(OPENTS_EXPERIMENTAL_CLANG_CL AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_property(SOURCE ${OPENTS_BGFX_SOURCES} APPEND PROPERTY
        COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/thirdparty/bx-clang-compat.h"
    )
endif()

# The texture loader decodes images with the stb_image header from bimg; no other file needs it.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui/rml/rmltexture.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bimg/3rdparty/stb"
)

message(STATUS "${PROJECT_NAME}: Adding compilier definitions...")
if(OPENTS_POSIX)
    target_compile_definitions(OpenTS PRIVATE NOMINMAX OPENTS_POSIX=1)
    target_include_directories(OpenTS PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/posix")
else()
    target_compile_definitions(OpenTS PRIVATE
        WIN32
        _WINDOWS
        NOMINMAX
    )
endif()

#
# ---------------------------------------------------------
# Windows libraries and linker options
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying linker options...")

# Required libraries:
#  Comctl32.lib
#  DbgHelp.lib
#  DSound.lib
#  Version.lib
#  Winmm.lib
#  wsock32.lib
#  + all Win32 libs
#

target_link_libraries(OpenTS PRIVATE
    VQALib

    bgfx
    bx
    bimg
    miniaudio
    lzo
    RmlUi::Core
    imgui
    SDL3::SDL3-static
)

if(OPENTS_POSIX)
    find_package(Threads REQUIRED)
    # FreeType draws the text platform/posix/gdicompat.cpp stands in GDI for.
    target_link_libraries(OpenTS PRIVATE freetype Threads::Threads ${CMAKE_DL_LIBS})
    if(APPLE)
        target_link_libraries(OpenTS PRIVATE
            "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
            "-framework UniformTypeIdentifiers"
            "-framework IOKit" "-framework CoreAudio" "-framework AudioToolbox"
            "-framework CoreFoundation"
        )
        # dbgprint.cpp reads the system version from its property list with libxml2, which
        # the macOS SDK carries.
        find_package(LibXml2 REQUIRED)
        target_link_libraries(OpenTS PRIVATE LibXml2::LibXml2)
        set_target_properties(OpenTS PROPERTIES
            OUTPUT_NAME "OpenTS"
            OUTPUT_NAME_DEBUG "OpenTS"
            OUTPUT_NAME_RELEASE "OpenTS"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/posix/macos/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "OpenTS"
            MACOSX_BUNDLE_GUI_IDENTIFIER "net.opents.OpenTS"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "OpenTS.icns"
        )
    endif()
else()
    target_link_libraries(OpenTS PRIVATE
    comctl32
    dbghelp
    iphlpapi
    psapi
    version
    winmm
    ws2_32
    kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32
    odbc32 odbccp32
    )
endif()

if(MSVC)
    target_link_options(OpenTS PRIVATE
        /SUBSYSTEM:WINDOWS
        /DEBUG
        /MAP
        # Raises the user address space of a 32-bit process from 2GB to 4GB under 64-bit
        # Windows. Pointers may then have the high bit set, so anything holding one in a
        # signed integer must only round-trip the bits and never compare or scale them. A
        # 64-bit image carries the flag already.
        /LARGEADDRESSAWARE
        # Declares the process per-monitor DPI aware, so a scaled desktop does not blur
        # the frame by stretching the whole window.
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/sun.manifest"
        /MANIFEST:EMBED
    )
endif()

#
# ---------------------------------------------------------
# Visual Studio source groups
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting up Visual Studio filters...")

# Init Source Files (_*.cpp)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.cpp$")
        source_group("Source Files\\Init Source Files" FILES ${f})
    endif()
endforeach()

# Init Header Files (_*.h)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.h$")
        source_group("Header Files\\Init Header Files" FILES ${f})
    endif()
endforeach()

# Define Header Files (*.hh)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.hh$")
        source_group("Header Files\\Define Header Files" FILES ${f})
    endif()
endforeach()

# Resource Files
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.(ico|cur|bmp|gif|jpe?g|rc)$")
        source_group("Resource Files" FILES ${f})
    endif()
endforeach()

# The interface headers the locomotors are written against.
source_group("Interface Files" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/iflyctrl.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/iloco.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipiggy.h"
)

# General source files
source_group("Source Files" REGULAR_EXPRESSION ".*\\.(c|cpp)$")
source_group("Header Files" REGULAR_EXPRESSION ".*\\.(h|hpp)$")

#
# ---------------------------------------------------------
# UI files
# ---------------------------------------------------------
#
# The game reads the ui directory from beside the executable. A separate target copies it
# there, so an edited document reaches the output without a relink.
# A macOS bundle keeps them in its Resources directory instead, where the game also looks.
if(APPLE)
    set(OPENTS_UI_DEST "$<TARGET_BUNDLE_CONTENT_DIR:OpenTS>/Resources/ui")
else()
    set(OPENTS_UI_DEST "$<TARGET_FILE_DIR:OpenTS>/ui")
endif()
add_custom_target(OpenTSUIFiles ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${OPENTS_UI_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "${OPENTS_UI_DEST}"
    COMMENT "Copying the UI files beside the executable"
    VERBATIM
)
add_dependencies(OpenTS OpenTSUIFiles)

#
# ---------------------------------------------------------
# Visual Studio startup project
# ---------------------------------------------------------
#
set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT OpenTS)

# ---------------------------------------------------------
# Add the .natvis file for Visual Studio debugging
# ---------------------------------------------------------
set(NATVIS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sun.natvis")
source_group("Natvis Files" FILES ${NATVIS_FILE})
if(MSVC)
    target_sources(OpenTS PRIVATE ${NATVIS_FILE})
    target_link_options(OpenTS PRIVATE "/NATVIS:${NATVIS_FILE}")
endif()
