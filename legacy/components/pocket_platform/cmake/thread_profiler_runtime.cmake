# Keep the upstream profiler's public 32-bit snapshot ABI. FreeRTOS writes into
# its configured counter type first; short-window deltas retain modulo-2^32
# semantics. Pocket's separate CPU monitor continues to use full 64-bit values.
function(ouse_profiler_replace_once text_var original replacement)
    set(text "${${text_var}}")
    string(REPLACE "${original}" "" without "${text}")
    string(LENGTH "${text}" before_length)
    string(LENGTH "${without}" after_length)
    string(LENGTH "${original}" match_length)
    math(EXPR removed "${before_length} - ${after_length}")
    if(NOT removed EQUAL match_length)
        message(FATAL_ERROR "Brookesia thread profiler changed; review runtime counter adapter")
    endif()
    string(REPLACE "${original}" "${replacement}" text "${text}")
    set(${text_var} "${text}" PARENT_SCOPE)
endfunction()

idf_component_get_property(ouse_profiler_lib brookesia_lib_utils COMPONENT_LIB)
idf_component_get_property(ouse_profiler_dir brookesia_lib_utils COMPONENT_DIR)
get_target_property(ouse_profiler_sources ${ouse_profiler_lib} SOURCES)
set(ouse_profiler_result "")
set(ouse_profiler_count 0)
foreach(ouse_source IN LISTS ouse_profiler_sources)
    get_filename_component(ouse_name "${ouse_source}" NAME)
    if(ouse_name STREQUAL "thread_profiler.cpp")
        if(NOT IS_ABSOLUTE "${ouse_source}")
            set(ouse_source "${ouse_profiler_dir}/${ouse_source}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ouse_source}")
        file(READ "${ouse_source}" ouse_text)
        ouse_profiler_replace_once(ouse_text
            "uxTaskGetSystemState(result->task_status.data(), array_size, &result->runtime);"
            "// Receive the native counter width before narrowing to the legacy public sample ABI.\n    configRUN_TIME_COUNTER_TYPE native_runtime = 0;\n    uxTaskGetSystemState(result->task_status.data(), array_size, &native_runtime);\n    result->runtime = static_cast<uint32_t>(native_runtime);")
        ouse_profiler_replace_once(ouse_text
            "uint32_t task_elapsed_time = end_copy[j].ulRunTimeCounter - start_copy[i].ulRunTimeCounter;"
            "const uint32_t task_elapsed_time = static_cast<uint32_t>(end_copy[j].ulRunTimeCounter - start_copy[i].ulRunTimeCounter);")
        ouse_profiler_replace_once(ouse_text
            "uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);"
            "const uint32_t percentage_time = static_cast<uint32_t>((static_cast<uint64_t>(task_elapsed_time) * 100ULL) / (static_cast<uint64_t>(total_elapsed_time) * portNUM_PROCESSORS));")
        ouse_profiler_replace_once(ouse_text
            "info.runtime_counter = end_copy[j].ulRunTimeCounter;"
            "info.runtime_counter = static_cast<uint32_t>(end_copy[j].ulRunTimeCounter);")
        set(ouse_generated "${CMAKE_CURRENT_BINARY_DIR}/ouse_profiler/thread_profiler.cpp")
        file(CONFIGURE OUTPUT "${ouse_generated}" CONTENT "${ouse_text}" @ONLY)
        foreach(ouse_property IN ITEMS COMPILE_FLAGS COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES)
            get_source_file_property(ouse_value "${ouse_source}" TARGET_DIRECTORY ${ouse_profiler_lib} ${ouse_property})
            if(ouse_value AND NOT ouse_value STREQUAL "NOTFOUND")
                set_source_files_properties("${ouse_generated}" TARGET_DIRECTORY ${ouse_profiler_lib}
                    PROPERTIES ${ouse_property} "${ouse_value}")
            endif()
        endforeach()
        list(APPEND ouse_profiler_result "${ouse_generated}")
        math(EXPR ouse_profiler_count "${ouse_profiler_count} + 1")
    else()
        list(APPEND ouse_profiler_result "${ouse_source}")
    endif()
endforeach()
if(NOT ouse_profiler_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one Brookesia thread_profiler.cpp")
endif()
set_property(TARGET ${ouse_profiler_lib} PROPERTY SOURCES "${ouse_profiler_result}")
message(STATUS "OusePocket: thread profiler accepts native FreeRTOS runtime counter width")
