#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/SpiMaster_SPI_DMA.hpp>
#include <coco/platform/SpiDisplayChannel_SPI_DMA.hpp>
#include <coco/board/config.hpp>


using namespace coco;


/// @brief Drivers for SpiMasterTest
/// Don't forget to lookup the alternate function number in the data sheet!
/// Also implement the interupt handler for the read DMA channel, check startup_XXX.s for the correct name
struct Drivers {
    Loop_TIM2 loop{APB1_TIMER_CLOCK};

    using SpiMaster = SpiMaster_SPI_DMA;
    SpiMaster spi{loop,
        gpio::PB3 | gpio::AF5 | gpio::Config::SPEED_MEDIUM, // SPI1 SCK (CN9 4)
        gpio::PB5 | gpio::AF5 | gpio::Config::SPEED_MEDIUM, // SPI1 MOSI (CN9 5)
        gpio::PB4 | gpio::AF5 | gpio::Config::PULL_UP, // SPI1 MISO (CN9 6)
        spi::SPI1_INFO,
        dma::DMA1_CH1_CH2_INFO};

        /*gpio::PB13 | gpio::AF5 | gpio::Config::SPEED_MEDIUM, // SPI2 SCK (CN10 30)
        gpio::PB15 | gpio::AF5 | gpio::Config::SPEED_MEDIUM, // SPI2 MOSI (CN10 26)
        gpio::PB14 | gpio::AF5 | gpio::Config::PULL_UP, // SPI2 MISO (CN10 28)
        spi::SPI2_INFO,
        dma::DMA1_CH1_CH2_INFO};*/

        /*gpio::PB3 | gpio::AF6 | gpio::Config::SPEED_MEDIUM, // SPI3 SCK (CN9 4)
        gpio::PB5 | gpio::AF6 | gpio::Config::SPEED_MEDIUM, // SPI3 MOSI (CN9 5)
        gpio::NONE, // no MISO, send only
        spi::SPI3_INFO,
        dma::DMA1_CH1_CH2_INFO};*/

    SpiMaster::Channel channel1{spi,
        gpio::PA9 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, // nCS (CN5 1)
        spi::Format::CLOCK_DIV_16 | spi::Format::PHA1_POL1 | spi::Format::DATA_8};
    SpiDisplayChannel_SPI_DMA channel2{spi,
        gpio::PC7 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, // nCS (CN5 2)
        gpio::PA8 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, false, 0xff, // D/nC (CN9 8)
        spi::Format::CLOCK_DIV_8 | spi::Format::PHA1_POL1 | spi::Format::DATA_8};
    SpiMaster::Buffer<1, 16> buffer1{channel1};
    SpiMaster::Buffer<1, 16> buffer2{channel2};
};

Drivers drivers;

extern "C" {
void DMA1_Channel1_IRQHandler() {
//debug::out << "IRQHandler\n";
    drivers.spi.DMA_Rx_IRQHandler();
}
}
