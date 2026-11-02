# AndroidX Games Frame Pacing 2.1.3, Apache-2.0. Pinned official Maven AAR.
set(SWAPPY_AAR "${CMAKE_BINARY_DIR}/swappy-2.1.3.aar")
set(SWAPPY_ROOT "${CMAKE_BINARY_DIR}/swappy-2.1.3")
file(DOWNLOAD
    "https://dl.google.com/dl/android/maven2/androidx/games/games-frame-pacing/2.1.3/games-frame-pacing-2.1.3.aar"
    "${SWAPPY_AAR}"
    EXPECTED_HASH SHA256=a9f83e0cf47d694ed2cea0ae995d5cf7411060d9d4b680610430d03d6b20ea27
    TLS_VERIFY ON)
if(NOT EXISTS "${SWAPPY_ROOT}/prefab/prefab.json")
    file(MAKE_DIRECTORY "${SWAPPY_ROOT}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${SWAPPY_AAR}"
        WORKING_DIRECTORY "${SWAPPY_ROOT}" RESULT_VARIABLE SWAPPY_EXTRACT_RESULT)
    if(NOT SWAPPY_EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not extract the verified Swappy AAR")
    endif()
endif()
add_library(rune_swappy STATIC IMPORTED GLOBAL)
set_target_properties(rune_swappy PROPERTIES
    IMPORTED_LOCATION "${SWAPPY_ROOT}/prefab/modules/swappy_static/libs/android.${ANDROID_ABI}/libswappy_static.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SWAPPY_ROOT}/prefab/modules/swappy_static/include")
