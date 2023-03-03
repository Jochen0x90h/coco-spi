#include <coco/loop.hpp>
#include <coco/debug.hpp>
#include <SpiMasterTest.hpp>


using namespace coco;

const uint8_t spiWriteData[] = {0x0a, 0x55};

Coroutine transferSpi(Buffer &buffer) {
	while (true) {
		buffer.set(0, spiWriteData);
		co_await buffer.transfer(Buffer::Op::READ_WRITE, 10);
		//co_await loop::sleep(1s);
		//debug::toggleRed();
	}
}


const uint8_t command[] = {0x00, 0xff};
const uint8_t data[] = {0x33, 0x55};

Coroutine writeCommandData(Buffer &buffer) {
	while (true) {
		buffer.set(0, command);
		buffer.set(2, data);
		co_await buffer.transfer(Buffer::Op::COMMAND2 | Buffer::Op::WRITE, 4);
		//co_await loop::sleep(1s);
	}
}


int main() {
	debug::init();
	Drivers drivers;

	transferSpi(drivers.transfer);
	writeCommandData({drivers.commandData});
	
	drivers.loop.run();
	return 0;
}
