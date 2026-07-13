#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/QuadSpiMaster_QUADSPI_DMA.hpp>
#include <coco/platform/SpiDisplayChannel_SPI_DMA.hpp>
#include <coco/board/config.hpp>


using namespace coco;

const gpio::Config qspiPins[] = {
    gpio::PB10 | gpio::AF10 | gpio::Config::SPEED_MEDIUM, // CLK (CN10 25)
    gpio::PC1 | gpio::AF10 | gpio::Config::SPEED_MEDIUM | gpio::Config::PULL_UP, // BK2_IO0 (CN7 36)
    gpio::PC2 | gpio::AF10 | gpio::Config::SPEED_MEDIUM | gpio::Config::PULL_UP, // BK2_IO1 (CN7 35)
    gpio::PC3 | gpio::AF10 | gpio::Config::SPEED_MEDIUM | gpio::Config::PULL_UP, // BK2_IO2 (CN7 37)
    gpio::PC4 | gpio::AF10 | gpio::Config::SPEED_MEDIUM | gpio::Config::PULL_UP // BK2_IO3 (CN10 35)
};

/// @brief Drivers for SpiMemory-Test
/// Don't forget to lookup the alternate function number in the data sheet!
/// Also implement the interupt handler for the read DMA channel, check startup_XXX.s for the correct name
struct Drivers {
    Loop_TIM2 loop{APB1_TIMER_CLOCK};

    using QuadSpiMaster = QuadSpiMaster_QUADSPI_DMA;
    QuadSpiMaster qspi{loop,
        qspi::QUADSPI_INFO,
        qspiPins,
        dma::DMA1_CH1_INFO};

    QuadSpiMaster::MemoryChannel channel1{qspi,
        gpio::PA9 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, // nCS (CN5 1)
        qspi::Format::CLOCK_DIV_256 | qspi::Format::MEMORY_16MB | qspi::Format::BANK_2,
        qspi::CommFormat::INSTRUCTION_1_LINE | qspi::CommFormat::ADDRESS_1_LINE | qspi::CommFormat::ADDRESS_24 | qspi::CommFormat::DATA_1_LINE,
        0x03, // read
        0x06, // write enable
        0x02, // write (256 byte boundary)
        0x20, // sector erase (4KB)
        0x05}; // read status
    QuadSpiMaster::Buffer<16> buffer1{channel1};
};

Drivers drivers;

extern "C" {
void QUADSPI_IRQHandler() {
    drivers.qspi.QUADSPI_IRQHandler();
}
}
