#include <coco/debug.hpp>
#include <SpiMasterTest.hpp>


using namespace coco;

const uint8_t spiWriteData[] = {0x0a, 0x55};

Coroutine transfer1(Loop &loop, Buffer &buffer) {
    int count = 0;
    while (buffer.ready()) {
        buffer.header<uint8_t>() = count++;
        //buffer.setHeaderType<uint8_t>(count & 1);
        //debug::toggleGreen();
        co_await buffer.write(spiWriteData);
        //co_await loop.sleep(100ms);
    }
}


const uint8_t command[] = {0x00, 0xff};
const uint8_t data[] = {0x33, 0x55};

Coroutine transfer2(Loop &loop, Buffer &buffer) {
    while (buffer.ready()) {
        //debug::toggleBlue();
        buffer.header<uint8_t>() = 1;
        co_await buffer.write(command);

        buffer.header<uint8_t>() = 0;
        co_await buffer.write(data);
    }
}


int main() {
    debug::out << "SpiMasterTest\n";

    transfer1(drivers.loop, drivers.buffer1);
    transfer2(drivers.loop, drivers.buffer2);

    drivers.loop.run();
    return 0;
}
