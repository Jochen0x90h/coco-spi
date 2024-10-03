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
		gpio::Config::PB3 | gpio::Config::AF5 | gpio::Config::SPEED_MEDIUM, // SPI1 SCK (CN9 4) (don't forget to lookup the alternate function number in the data sheet!)
		gpio::Config::PB4 | gpio::Config::AF5 | gpio::Config::PULL_UP, // SPI1 MISO (CN9 6)
		gpio::Config::PB5 | gpio::Config::AF5 | gpio::Config::SPEED_MEDIUM, // SPI1 MOSI (CN9 5)
		gpio::Config::PA8 | gpio::Config::SPEED_MEDIUM, // DC (CN9 8)
		spi::SPI1_INFO,
		dma::DMA1_CH1_CH2_INFO,
		spi::Config::CLOCK_DIV8 | spi::Config::PHA1_POL1 | spi::Config::DATA_8};
	SpiMaster::Channel channel1{spi, gpio::Config::PA9 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT}; // nCS (CN5 1)
	SpiMaster::Channel channel2{spi, gpio::Config::PC7 | gpio::Config::SPEED_MEDIUM | gpio::Config::INVERT, true}; // nCS (CN5 2)
	SpiMaster::Buffer<16> buffer1{channel1};
	SpiMaster::Buffer<16> buffer2{channel2};
};

Drivers drivers;

extern "C" {
void DMA1_Channel1_IRQHandler() {
	drivers.spi.DMA_Rx_IRQHandler();
}
}
