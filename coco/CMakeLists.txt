set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(${PROJECT_NAME})
#target_sources(${PROJECT_NAME}
#	PUBLIC FILE_SET headers TYPE HEADERS FILES
#	PRIVATE
#)

if(${PLATFORM} STREQUAL "native" OR ${PLATFORM} STREQUAL "emu")
    # native platform (Windows, MacOS, Linux)
    target_sources(${PROJECT_NAME}
        PUBLIC FILE_SET platform_headers TYPE HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/native FILES
            native/coco/platform/dummy.hpp
        PRIVATE
            native/coco/platform/dummy.cpp
    )
elseif(${PLATFORM} MATCHES "^nrf52")
    target_sources(${PROJECT_NAME}
        PUBLIC FILE_SET platform_headers TYPE HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/nrf52 FILES
            nrf52/coco/platform/SpiMaster_SPIM.hpp
            nrf52/coco/platform/SpiDisplayChannel_SPIM.hpp
        PRIVATE
            nrf52/coco/platform/SpiMaster_SPIM.cpp
            nrf52/coco/platform/SpiDisplayChannel_SPIM.cpp
    )
elseif(${PLATFORM} MATCHES "^stm32")
    target_sources(${PROJECT_NAME}
        PUBLIC FILE_SET platform_headers TYPE HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/stm32 FILES
            stm32/coco/platform/SpiMaster_SPI_DMA.hpp
            stm32/coco/platform/SpiDisplayChannel_SPI_DMA.hpp
        PRIVATE
            stm32/coco/platform/SpiMaster_SPI_DMA.cpp
            stm32/coco/platform/SpiDisplayChannel_SPI_DMA.cpp
    )
    if(${PLATFORM} MATCHES "^stm32f4" OR ${PLATFORM} MATCHES "^stm32l4" OR ${PLATFORM} MATCHES "^stm32g4")
        target_sources(${PROJECT_NAME}
            PUBLIC FILE_SET platform_headers FILES
                stm32/coco/platform/SpiMemory_QUADSPI_DMA.hpp
            PRIVATE
                stm32/coco/platform/SpiMemory_QUADSPI_DMA.cpp
        )
    endif()
endif()

target_link_libraries(${PROJECT_NAME}
    coco::coco
    coco-loop::coco-loop
    coco-device::coco-device
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ..
)

# install the library
install(TARGETS ${PROJECT_NAME}
    FILE_SET headers DESTINATION include/coco
    FILE_SET platform_headers DESTINATION include
)
