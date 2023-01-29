#include <coco/loop.hpp>
#include <coco/debug.hpp>
#include <SpiMasterTest.hpp>


using namespace coco;

uint8_t spiWriteData[] = {0x0a, 0x55};
uint8_t spiReadData[10];

Coroutine transferSpi(SpiMaster &spi) {
	while (true) {
		co_await spi.transfer(spiWriteData, 2, spiReadData, 10);
		//co_await loop::sleep(1s);
		//debug::toggleRed();
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
		//co_await loop::sleep(1s);
	}
}


int main() {
	debug::init();
	Drivers drivers;

	transferSpi(drivers.transfer);
	writeCommandData({drivers.command, drivers.data});
	
	drivers.loop.run();
	return 0;
}
