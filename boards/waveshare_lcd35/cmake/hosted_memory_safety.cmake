idf_component_get_property(ouse_memory_lib espressif__esp_hosted COMPONENT_LIB)
idf_component_get_property(ouse_memory_dir espressif__esp_hosted COMPONENT_DIR)
get_target_property(ouse_memory_sources "${ouse_memory_lib}" SOURCES)
set(ouse_memory_result "")
set(ouse_memory_count 0)
foreach(ouse_source IN LISTS ouse_memory_sources)
    if(NOT IS_ABSOLUTE "${ouse_source}")
        get_filename_component(ouse_source "${ouse_source}" ABSOLUTE BASE_DIR "${ouse_memory_dir}")
    endif()
    get_filename_component(ouse_name "${ouse_source}" NAME)
    if(ouse_name STREQUAL "sdio_drv.c" OR ouse_name STREQUAL "mempool.c")
        file(READ "${ouse_source}" ouse_memory_text)
        string(REPLACE "\r\n" "\n" ouse_memory_text "${ouse_memory_text}")
        string(SHA256 ouse_memory_hash "${ouse_memory_text}")
        if(ouse_name STREQUAL "sdio_drv.c")
            if(NOT ouse_memory_hash STREQUAL "2d995b6a74652b02b81a5ed612586b2e7cdda180e2afadaef3f969ffade965cb")
                message(FATAL_ERROR "Hosted SDIO source changed; review the low-memory overlay")
            endif()
            string(REPLACE "\t\t\tassert(sendbuf);" "" ouse_memory_text "${ouse_memory_text}")
            string(REPLACE "\t\tassert(*buf);" [=[
        if (!*buf) {
            double_buf.buffer[index].buf_size = 0;
            return NULL;
        }
]=] ouse_memory_text "${ouse_memory_text}")
            string(REPLACE "\t\tassert(rxbuff);" [=[
        if (!rxbuff) {
            SDIO_DRV_UNLOCK();
            g_h.funcs->_h_msleep(10);
            continue;
        }
]=] ouse_memory_text "${ouse_memory_text}")
            string(REPLACE "\t\t\t\tassert(copy_payload);" [=[
                if (!copy_payload) {
                    H_FREE_PTR_WITH_FUNC(buf_handle->free_buf_handle, buf_handle->priv_buffer_handle);
                    ESP_LOGW(TAG, "RX payload memory pressure; dropping packet");
                    continue;
                }
]=] ouse_memory_text "${ouse_memory_text}")
            string(REPLACE "\t\tassert(pkt_rxbuff);" [=[
        if (!pkt_rxbuff) {
            for (unsigned retry = 0; retry < 20 && !pkt_rxbuff; ++retry) {
                g_h.funcs->_h_msleep(5);
                pkt_rxbuff = sdio_buffer_alloc(MEMSET_REQUIRED);
            }
            if (!pkt_rxbuff) {
                ESP_LOGW(TAG, "RX memory pressure; dropping stream instead of aborting");
                return ESP_ERR_NO_MEM;
            }
        }
]=] ouse_memory_text "${ouse_memory_text}")
            string(REPLACE "\t\t\t\t\tpacket_size, buf_len);\n\t\t\treturn ESP_FAIL;"
                "\t\t\t\t\tpacket_size, buf_len);\n            sdio_buffer_free(pkt_rxbuff);\n\t\t\treturn ESP_FAIL;"
                ouse_memory_text "${ouse_memory_text}")
        else()
            if(NOT ouse_memory_hash STREQUAL "71fd137e67b2c580ab5cd57b7532c77fb95eb6bde09bc517c0442d58251ec594")
                message(FATAL_ERROR "Hosted pool source changed; review the low-memory overlay")
            endif()
            string(REPLACE "\tSLIST_INSERT_HEAD(&(mp->head), (struct mempool_entry *)mem, entries);" [=[
    unsigned cached = 0;
    struct mempool_entry *entry;
    SLIST_FOREACH(entry, &(mp->head), entries) { ++cached; }
    if (cached >= 4) {
        g_h.funcs->_h_unlock_mempool(mp->spinlock);
        g_h.funcs->_h_free_align(mem);
        return;
    }
    SLIST_INSERT_HEAD(&(mp->head), (struct mempool_entry *)mem, entries);
]=] ouse_memory_text "${ouse_memory_text}")
        endif()
        set(ouse_generated "${CMAKE_CURRENT_BINARY_DIR}/ouse_hosted_memory/${ouse_name}")
        file(CONFIGURE OUTPUT "${ouse_generated}" CONTENT "${ouse_memory_text}" @ONLY)
        get_filename_component(ouse_original_dir "${ouse_source}" DIRECTORY)
        set_source_files_properties("${ouse_generated}" TARGET_DIRECTORY "${ouse_memory_lib}"
            PROPERTIES INCLUDE_DIRECTORIES "${ouse_original_dir}")
        foreach(ouse_property IN ITEMS COMPILE_FLAGS COMPILE_OPTIONS COMPILE_DEFINITIONS)
            get_source_file_property(ouse_value "${ouse_source}" TARGET_DIRECTORY "${ouse_memory_lib}" "${ouse_property}")
            if(ouse_value AND NOT ouse_value STREQUAL "NOTFOUND")
                set_source_files_properties("${ouse_generated}" TARGET_DIRECTORY "${ouse_memory_lib}"
                    PROPERTIES "${ouse_property}" "${ouse_value}")
            endif()
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ouse_source}")
        list(APPEND ouse_memory_result "${ouse_generated}")
        math(EXPR ouse_memory_count "${ouse_memory_count} + 1")
    else()
        list(APPEND ouse_memory_result "${ouse_source}")
    endif()
endforeach()
if(NOT ouse_memory_count EQUAL 2)
    message(FATAL_ERROR "Expected exactly two Hosted low-memory overlay sources")
endif()
set_property(TARGET "${ouse_memory_lib}" PROPERTY SOURCES "${ouse_memory_result}")
message(STATUS "OusePocket: bounded Hosted DMA buffer cache and recoverable RX/TX memory pressure; managed sources unchanged")
