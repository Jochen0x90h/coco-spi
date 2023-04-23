#include <coco/debug.hpp>
#include <SpiMasterTest.hpp>


using namespace coco;

const uint8_t spiWriteData[] = {0x0a, 0x55};

Coroutine transfer(Loop &loop, Buffer &buffer) {
	while (true) {
		co_await buffer.writeArray(spiWriteData);
		//co_await loop.sleep(1s);
		//debug::toggleGreen();
	}
}


const uint8_t command[] = {0x00, 0xff};
const uint8_t data[] = {0x33, 0x55};

Coroutine writeCommandData(Loop &loop, Buffer &buffer) {
	while (true) {
		buffer.setHeader(command);
		co_await buffer.writeArray(data);
		//co_await loop.sleep(1s);
		//debug::toggleBlue();
	}
}


int main() {
	debug::init();
	Drivers drivers;

	transfer(drivers.loop, drivers.transfer);
	writeCommandData(drivers.loop, drivers.commandData);

	drivers.loop.run();
	return 0;
}
