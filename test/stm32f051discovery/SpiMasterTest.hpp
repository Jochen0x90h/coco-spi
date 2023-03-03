#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/SpiMaster_SPI1.hpp>


using namespace coco;

// drivers for SpiTest
struct Drivers {
	Loop_TIM2 loop;
	SpiMaster_SPI1 spi{loop,
		SpiMaster_SPI1::Prescaler::DIV8,
		gpio::PA(5), // SCK
		gpio::PA(7), // MOSI
		gpio::PA(6)}; // MISO
	SpiMaster_SPI1::Buffer<16> transfer{spi,  gpio::PA(3)};
	SpiMaster_SPI1::Buffer<16> commandData{spi, gpio::PA(4)};
};
