#include <coco/debug.hpp>
#include <SpiMasterTest.hpp>


using namespace coco;

const uint8_t spiWriteData[] = {0x0a, 0x55};

Coroutine transfer1(Loop &loop, Buffer &buffer) {
	while (buffer.ready()) {
		debug::toggleGreen();
		co_await buffer.writeArray(spiWriteData);
		//co_await loop.sleep(100ms);
	}
}


const uint8_t command[] = {0x00, 0xff};
const uint8_t data[] = {0x33, 0x55};

Coroutine transfer2(Loop &loop, Buffer &buffer) {
	while (buffer.ready()) {
		//debug::toggleBlue();
		buffer.setHeader(command);
		co_await buffer.writeArray(data);//, Buffer::Op::COMMAND);
	}
}


int main() {
	transfer1(drivers.loop, drivers.buffer1);
	transfer2(drivers.loop, drivers.buffer2);

	drivers.loop.run();
	return 0;
}
