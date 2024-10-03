#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/SpiMaster_SPI_DMA.hpp>
#include <coco/board/config.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
	Loop_TIM2 loop{APB1_TIMER_CLOCK};

	using SpiMaster = SpiMaster_SPI_DMA;
	SpiMaster spi{loop,
		gpio::Config::PA5 | gpio::Config::AF5 | gpio::Config::SPEED_MEDIUM, // SPI1 SCK (don't forget to lookup the alternate function number in the data sheet!)
		gpio::Config::PA6 | gpio::Config::AF5 | gpio::Config::PULL_UP, // SPI1 MISO
		gpio::Config::PA7 | gpio::Config::AF5 | gpio::Config::SPEED_MEDIUM, // SPI1 MOSI
		gpio::Config::PA8 | gpio::Config::SPEED_MEDIUM, // DC
		spi::SPI1_INFO,
		dma::DMA1_CH2_CH3_INFO,

		spi::Config::CLOCK_DIV8 | spi::Config::PHA1_POL1 | spi::Config::DATA_8};
	SpiMaster::Channel channel1{spi, gpio::Config::PA3 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT}; // nCS
	SpiMaster::Channel channel2{spi, gpio::Config::PA4 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, true}; // nCS
	SpiMaster::Buffer<16> buffer1{channel1};
	SpiMaster::Buffer<16> buffer2{channel2};
};

Drivers drivers;

extern "C" {
void DMA1_Channel2_IRQHandler() {
	drivers.spi.DMA_Rx_IRQHandler();
}
}
