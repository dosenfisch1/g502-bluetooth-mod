# esp_hid_gap.c/.h stammen aus dem ESP-IDF-Beispiel und werden bei Bedarf kopiert
set(GAP_SRC_DIR "$ENV{IDF_PATH}/examples/bluetooth/esp_hid_device/main")
foreach(f esp_hid_gap.c esp_hid_gap.h)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${f}")
        if(EXISTS "${GAP_SRC_DIR}/${f}")
            file(COPY "${GAP_SRC_DIR}/${f}" DESTINATION "${CMAKE_CURRENT_LIST_DIR}")
        else()
            message(FATAL_ERROR "${f} nicht gefunden. Bitte manuell aus ${GAP_SRC_DIR} nach main/ kopieren.")
        endif()
    endif()
endforeach()

idf_component_register(
    SRCS "main.c" "esp_hid_gap.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_hid bt nvs_flash usb
)
