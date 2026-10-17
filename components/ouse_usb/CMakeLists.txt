idf_component_register(SRCS "ouse_usb.c" "ouse_usb_descriptors.c" "ouse_usb_network.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "."
    REQUIRES ouse_devices
    PRIV_REQUIRES esp_tinyusb esp_netif esp_event esp_timer esp_hw_support lwip)

# esp_tinyusb supplies the platform config; add the audio class consistently to
# the stack and its consumers without modifying managed vendor sources.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
    target_compile_options(${tusb_lib} PUBLIC
        "-include${CMAKE_CURRENT_LIST_DIR}/ouse_usb_audio_config.h")
    # Local bounded overlay for 0.21.0~2: prime explicit feedback before sending
    # it and keep OUT armed after a legal ZLP. Managed sources remain untouched.
    idf_component_get_property(tusb_dir espressif__tinyusb COMPONENT_DIR)
    set(audio_original "${tusb_dir}/src/class/audio/audio_device.c")
    set(audio_overlay "${CMAKE_CURRENT_BINARY_DIR}/audio_device_ouse.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${audio_original}" "${CMAKE_CURRENT_LIST_DIR}/patch_audio_driver.py")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/patch_audio_driver.py"
        "${audio_original}" "${audio_overlay}" COMMAND_ERROR_IS_FATAL ANY)
    get_target_property(tusb_sources ${tusb_lib} SOURCES)
    set(audio_replaced FALSE)
    set(tusb_overlay_sources "")
    foreach(source IN LISTS tusb_sources)
        if(source MATCHES "(^|/)class/audio/audio_device\\.c$")
            list(APPEND tusb_overlay_sources "${audio_overlay}")
            set(audio_replaced TRUE)
        else()
            list(APPEND tusb_overlay_sources "${source}")
        endif()
    endforeach()
    if(NOT audio_replaced)
        message(FATAL_ERROR "Pinned TinyUSB audio source not found in component target")
    endif()
    set_property(TARGET ${tusb_lib} PROPERTY SOURCES "${tusb_overlay_sources}")
    target_include_directories(${tusb_lib} PRIVATE "${tusb_dir}/src/class/audio")
    idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
    target_compile_definitions(${lwip_lib} PRIVATE IP_NAPT_MAX=64)
endif()
