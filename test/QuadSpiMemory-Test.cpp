#include <coco/convert.hpp>
#include <coco/debug.hpp>
#include <QuadSpiMemory-Test.hpp>


using namespace coco;

const uint8_t writeData[] = {0x0a, 0x55};

Coroutine transfer1(Loop &loop, Buffer &buffer) {
    int address = 0;
    while (address < 5839) {
        buffer.header<uint32_t>() = address;
        co_await buffer.read();
        address += buffer.capacity();

        debug::out << buffer.string();

    }


    //buffer.header<uint32_t>() = 2;
    //debug::out << "write\n";
    //co_await buffer.writeData(writeData, 2);
/*
    buffer.header<uint32_t>() = 0;
    debug::out << "erase\n";
    co_await buffer.erase();

    while (buffer.ready()) {
        buffer.header<uint32_t>() = 0;
        debug::out << "read\n";
        co_await buffer.read(2);
        //debug::out << "write\n";
        //co_await buffer.writeData(writeData, 2);

        debug::out << hex(buffer[0]) << ' ' << hex(buffer[1]) << '\n';

        co_await loop.sleep(1000ms);
    }
*/
}

/*
const uint8_t command[] = {0x00, 0xff};
const uint8_t data[] = {0x33, 0x55};

Coroutine transfer2(Loop &loop, Buffer &buffer) {
    while (buffer.ready()) {
        //debug::toggleBlue();
        buffer.setHeader(command);
        co_await buffer.writeArray(data);//, Buffer::Op::COMMAND);
    }
}*/


int main() {
    debug::out << "SpiMemoryTest\n";

    transfer1(drivers.loop, drivers.buffer1);
    //transfer2(drivers.loop, drivers.buffer2);

    drivers.loop.run();
    return 0;
}
