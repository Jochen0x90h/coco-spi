#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/SpiMaster_XSPI_DMA.hpp>
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

    using SpiMaster = SpiMaster_XSPI_DMA;
    SpiMaster qspi{loop,
        xspi::QUADSPI_INFO,
        qspiPins,
        dma::DMA1_CH1_INFO};

    SpiMaster::MemoryChannel channel1{qspi,
        gpio::PA9 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, // nCS (CN5 1)
        xspi::Format::CLOCK_DIV_256 | xspi::Format::MEMORY_16MB | xspi::Format::BANK_2, xspi::Timing::DEFAULT,
        3, // address bytes
        xspi::MODE_1_1_1, 0x03, 0, // read
        xspi::MODE_1_1_1, 0x06, // write enable
        xspi::MODE_1_1_1, 0x02, 0, // write (256 byte boundary)
        xspi::MODE_1_1_1, 0x20, // sector erase (4KB)
        xspi::MODE_1_1_1, 0x05}; // read status
    SpiMaster::Buffer<16> buffer1{channel1};
};

Drivers drivers;

extern "C" {
void QUADSPI_IRQHandler() {
    drivers.qspi.XSPI_IRQHandler();
}
}
