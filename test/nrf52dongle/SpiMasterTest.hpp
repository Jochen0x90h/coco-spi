#pragma once

#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/SpiMaster_SPIM3.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
	Loop_RTC0 loop;

	using SpiMaster = SpiMaster_SPIM3;
	SpiMaster spi{loop,
		gpio::Config::P0_3, // SCK
		gpio::Config::P0_21 | gpio::Config::PULL_UP, // MISO
		gpio::Config::P0_2, // MOSI
		gpio::Config::P0_21 | gpio::Config::PULL_UP, // DC (data/command for write-only display, can be same as MISO)
		//gpio::Config::P0_8, // DC (data/command)
		spi::Config::SPEED_1M | spi::Config::PHA0_POL0 | spi::Config::MSB_FIRST};
	SpiMaster::Channel channel1{spi, gpio::Config::P0_20 | gpio::Config::INVERT}; // nCS
	SpiMaster::Channel channel2{spi, gpio::Config::P0_19 | gpio::Config::INVERT, true}; // nCS
	SpiMaster::Buffer<16> buffer1{channel1};
	SpiMaster::Buffer<16> buffer2{channel2};
};

Drivers drivers;

extern "C" {
void SPIM3_IRQHandler() {
	drivers.spi.SPIM3_IRQHandler();
}
}
