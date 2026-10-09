cmake_minimum_required(VERSION 3.20)

set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

project(LPPCFileChecker VERSION 0.1.0 LANGUAGES CXX)

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

option(LPPC_FILE_CHECKER_BUILD_DLL "Build the EuroScope DLL" ON)

set(EUROSCOPE_INCLUDE_DIR "" CACHE PATH "Directory that contains EuroScopePlugIn.h")
set(EUROSCOPE_LIBRARY "" CACHE FILEPATH "Path to EuroScopePlugInDLL.lib")

set(_euroscope_default_root "$ENV{USERPROFILE}/AppData/Roaming/EuroScope/PlugIn")
if(EXISTS "${_euroscope_default_root}/EuroScopePlugIn.h" AND EXISTS "${_euroscope_default_root}/EuroScopePlugInDll.lib")
    if(EUROSCOPE_INCLUDE_DIR STREQUAL "")
        set(EUROSCOPE_INCLUDE_DIR "${_euroscope_default_root}" CACHE PATH "Directory that contains EuroScopePlugIn.h" FORCE)
    endif()

    if(EUROSCOPE_LIBRARY STREQUAL "")
        set(EUROSCOPE_LIBRARY "${_euroscope_default_root}/EuroScopePlugInDll.lib" CACHE FILEPATH "Path to EuroScopePlugInDll.lib" FORCE)
    endif()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This plugin must be built as Win32/x86 because EuroScope is 32-bit. Reconfigure the project with -A Win32 (or switch the Visual Studio platform to Win32).")
endif()

add_library(file_checker_core STATIC
    src/FileCheckerCore.cpp
)

target_include_directories(file_checker_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(file_checker_core
    PUBLIC
        winhttp
        ole32
        shell32
)

if(LPPC_FILE_CHECKER_BUILD_DLL)
    if(NOT EXISTS "${EUROSCOPE_INCLUDE_DIR}/EuroScopePlugIn.h")
        message(FATAL_ERROR "Set EUROSCOPE_INCLUDE_DIR to the folder that contains EuroScopePlugIn.h")
    endif()

    if(NOT EXISTS "${EUROSCOPE_LIBRARY}")
        message(FATAL_ERROR "Set EUROSCOPE_LIBRARY to the EuroScopePlugInDLL.lib path")
    endif()

    add_executable(LPPCUpdateWizard WIN32 src/UpdateWizard.cpp)
    target_link_libraries(LPPCUpdateWizard PRIVATE file_checker_core user32)
    set_target_properties(LPPCUpdateWizard PROPERTIES OUTPUT_NAME "LPPCUpdateWizard")

    set(WIZARD_RC "${CMAKE_CURRENT_BINARY_DIR}/WizardResource.rc")
    set(WIZARD_RC_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/GenerateWizardResource.cmake")
    file(GENERATE OUTPUT "${WIZARD_RC_SCRIPT}" CONTENT [==[
set(_rc "${CMAKE_CURRENT_LIST_DIR}/WizardResource.rc")
set(_wizard "$<TARGET_FILE:LPPCUpdateWizard>")
file(SIZE "${_wizard}" _size)
file(TIMESTAMP "${_wizard}" _stamp "%Y%m%d%H%M%S")
file(WRITE "${_rc}" "1 RCDATA \"${_wizard}\"\n// wizard build ${_stamp} size ${_size}\n")
]==])
    add_custom_command(
        OUTPUT "${WIZARD_RC}"
        COMMAND "${CMAKE_COMMAND}" -P "${WIZARD_RC_SCRIPT}"
        DEPENDS LPPCUpdateWizard "${WIZARD_RC_SCRIPT}"
        COMMENT "Generating wizard resource file"
        VERBATIM)
    set_source_files_properties("${WIZARD_RC}" PROPERTIES GENERATED TRUE)

    add_library(LPPCFileChecker SHARED
        src/FileCheckerPlugin.cpp
        "${WIZARD_RC}"
    )
    add_dependencies(LPPCFileChecker LPPCUpdateWizard)

    target_include_directories(LPPCFileChecker
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${EUROSCOPE_INCLUDE_DIR}
    )

    target_link_libraries(LPPCFileChecker
        PRIVATE
            file_checker_core
            ${EUROSCOPE_LIBRARY}
            winhttp
            user32
    )

    set_target_properties(LPPCFileChecker PROPERTIES
        OUTPUT_NAME "LPPCFileChecker"
    )
endif()