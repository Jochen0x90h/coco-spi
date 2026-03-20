#pragma once

#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/SpiMaster_SPIM.hpp>
#include <coco/platform/SpiDisplayChannel_SPIM.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
    Loop_RTC0 loop;

    using SpiMaster = SpiMaster_SPIM;
    SpiMaster spi{loop,
        gpio::P0_3, // SCK
        gpio::P0_2, // MOSI
        gpio::P0_21 | gpio::Config::PULL_UP, // MISO
        spim::SPIM0_INFO};
        //spim::SPIM3_INFO};

    SpiMaster::Channel channel1{spi,
        gpio::P0_20 | gpio::Config::INVERT, // nCS
        spim::Format::FREQUENCY_500K | spim::Format::PHA1_POL1 | spim::Format::DATA_8};
    SpiDisplayChannel_SPIM channel2{spi,
        gpio::P0_19 | gpio::Config::INVERT, // nCS
        gpio::P0_21 | gpio::Config::INVERT, true, 0xff, // D/nC shared
        //gpio::P0_4 | gpio::Config::INVERT, false, 0xff, // D/nC
        spim::Format::FREQUENCY_1M | spim::Format::PHA1_POL1 | spim::Format::DATA_8};
    SpiMaster::Buffer<0, 16> buffer1{channel1};
    SpiMaster::Buffer<2, 16> buffer2{channel2};
};

Drivers drivers;

extern "C" {
void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler() {
//void SPIM3_IRQHandler() {
    drivers.spi.SPIM_IRQHandler();
}
}
