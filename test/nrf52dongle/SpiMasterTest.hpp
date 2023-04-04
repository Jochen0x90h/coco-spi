#pragma once

#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/SpiMaster_SPIM3.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
	Loop_RTC0 loop;
	SpiMaster_SPIM3 spi{loop,
		SpiMaster_SPIM3::Speed::M1,
		gpio::P0(19), // SCK
		gpio::P0(20), // MOSI
		gpio::P0(21), // MISO
		gpio::P0(21)}; // DC (data/command for write-only display, can be same as MISO)
	SpiMaster_SPIM3::Channel channel1{spi, gpio::P0(2)};
	SpiMaster_SPIM3::Channel channel2{spi, gpio::P0(3), true};
	SpiMaster_SPIM3::Buffer<16> transfer{channel1};
	SpiMaster_SPIM3::Buffer<16> commandData{channel2};
};
