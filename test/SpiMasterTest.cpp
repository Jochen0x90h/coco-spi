#include <coco/loop.hpp>
#include <coco/debug.hpp>
#include <coco/board/SpiTest.hpp>


using namespace coco;

uint8_t spiWriteData[] = {0x0a, 0x55};
uint8_t spiReadData[10];

Coroutine transferSpi(SpiMaster &spi) {
	while (true) {
		co_await spi.transfer(spiWriteData, 2, spiReadData, 10);
		//co_await Timer::sleep(100ms);
		//Debug::toggleRedLed();
	}
}


uint8_t command[] = {0x00, 0xff};
uint8_t data[] = {0x33, 0x55};

struct Spi {
	SpiMaster &command;
	SpiMaster &data;
};

Coroutine writeCommandData(Spi spi) {
	while (true) {
		co_await spi.command.write(command, 2);
		co_await spi.data.write(data, 2);
	}
}


int main() {
	loop::init();
	debug::init();
	board::SpiTest drivers;

	transferSpi(drivers.transfer);
	writeCommandData({drivers.command, drivers.data});
	
	loop::run();
}
