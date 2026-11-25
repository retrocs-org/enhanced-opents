if(OPENTS_POSIX)
    # Off Windows there is no resource DLL. The string tables of language.rc are compiled into
    # a generated table that the game links directly; see platform/posix/langstrings.cpp.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(OPENTS_LANGTABLE "${OPENTS_GENERATED_DIR}/opents_langtable.inc")
    add_custom_command(
        OUTPUT "${OPENTS_LANGTABLE}"
        COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/rcstrings.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/language.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/language.h"
            "${OPENTS_LANGTABLE}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/rcstrings.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/language.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/language.h"
        COMMENT "Generating the string table from language.rc"
        VERBATIM
    )
    add_custom_target(OpenTSLanguageTable DEPENDS "${OPENTS_LANGTABLE}")
    add_dependencies(OpenTS OpenTSLanguageTable)
    return()
endif()

file(GLOB_RECURSE LANG_SRC CONFIGURE_DEPENDS
    "*.cpp" "*.c" "*.cc" "*.rc"
    "*.h" "*.hpp" "*.hm"
)

add_library(Language SHARED ${LANG_SRC})
target_compile_features(Language PRIVATE cxx_std_20)
target_compile_definitions(Language PRIVATE NOMINMAX)

# The version resource is stamped from the generated headers, which the resource compiler
# reaches through the target's include paths.
target_include_directories(Language PRIVATE "${OPENTS_GENERATED_DIR}")
add_dependencies(Language OpenTSBuildStamp)

# The game loads its strings from this library at runtime, so building the executable alone
# would leave a stale Language.dll beside it.
add_dependencies(OpenTS Language)

if(MSVC)
    target_link_options(Language PRIVATE "/NOENTRY" "/NODEFAULTLIB")
endif()

# The game loads this library by name from its own directory, so it is built beside the
# executable rather than copied there.
set_target_properties(Language PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
