cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5stack_pets_basic_pet)

set(SPIFFS_ASSET_DIR "${CMAKE_BINARY_DIR}/spiffs_assets")
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
file(REMOVE_RECURSE "${SPIFFS_ASSET_DIR}")
file(MAKE_DIRECTORY "${SPIFFS_ASSET_DIR}/aomi" "${SPIFFS_ASSET_DIR}/bitomos-umi")

foreach(PET_ID aomi bitomos-umi)
    set(PNG_ASSET_DIR "${REPO_ROOT}/assets/${PET_ID}/display-96-png")
    if(EXISTS "${PNG_ASSET_DIR}")
        file(COPY "${PNG_ASSET_DIR}" DESTINATION "${SPIFFS_ASSET_DIR}/${PET_ID}")
    else()
        message(WARNING "PNG display assets not found: ${PNG_ASSET_DIR}")
    endif()
endforeach()

spiffs_create_partition_image(storage "${SPIFFS_ASSET_DIR}" FLASH_IN_PROJECT)
