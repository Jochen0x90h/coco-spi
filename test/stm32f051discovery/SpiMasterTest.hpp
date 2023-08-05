#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/SpiMaster_SPI1_DMA.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
	Loop_TIM2 loop;
	using SpiMaster = SpiMaster_SPI1_DMA;
	SpiMaster spi{loop,
		SpiMaster::Prescaler::DIV8,
		{gpio::PA(5), 0}, // SCK
		{gpio::PA(7), 0}, // MOSI
		{gpio::PA(6), 0}, // MISO
		gpio::PA(8)}; // DC
	SpiMaster::Channel channel1{spi, gpio::PA(3)};
	SpiMaster::Channel channel2{spi, gpio::PA(4), true};
	SpiMaster::Buffer<4, 16> transfer{channel1};
	SpiMaster::Buffer<4, 16> commandData{channel2};
};
