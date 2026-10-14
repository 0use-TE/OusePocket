#
# ESP Platform
#
idf_component_register(
    SRCS ${COMPONENT_SRCS_C} ${COMPONENT_SRCS_CPP}
    INCLUDE_DIRS ${COMPONENT_INCLUDE_DIRS}
    PRIV_INCLUDE_DIRS ${COMPONENT_PRIVATE_INCLUDE_DIRS}
    REQUIRES ${COMPONENT_REQUIRES} nvs_flash pocket_platform
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(${COMPONENT_LIB} PUBLIC "-u app_settings_provider_symbol")

set(app_settings_package_id "brookesia.general.settings")
if(DEFINED BROOKESIA_SYSTEM_CORE_COMPONENT_DIR AND NOT "${BROOKESIA_SYSTEM_CORE_COMPONENT_DIR}" STREQUAL "")
    set(brookesia_system_core_dir "${BROOKESIA_SYSTEM_CORE_COMPONENT_DIR}")
else()
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if("espressif__brookesia_system_core" IN_LIST build_components)
        idf_component_get_property(brookesia_system_core_dir espressif__brookesia_system_core COMPONENT_DIR)
    else()
        idf_component_get_property(brookesia_system_core_dir brookesia_system_core COMPONENT_DIR)
    endif()
endif()
include("${brookesia_system_core_dir}/cmake/runtime_app_stage.cmake")
brookesia_system_core_get_esp_runtime_app_stage_root(app_settings_stage_root)
brookesia_stage_runtime_app_package(
    PACKAGE_ID "${app_settings_package_id}"
    SOURCE_DIR "${COMPONENT_DIR}/package"
    STAGE_ROOT "${app_settings_stage_root}"
    NO_INDEX
)

include(package_manager)
cu_pkg_get_version(${COMPONENT_DIR} ver_major ver_minor ver_patch)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    BROOKESIA_APP_SETTINGS_VER_MAJOR=${ver_major}
    BROOKESIA_APP_SETTINGS_VER_MINOR=${ver_minor}
    BROOKESIA_APP_SETTINGS_VER_PATCH=${ver_patch})
