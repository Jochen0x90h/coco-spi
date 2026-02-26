#include "SpiDisplayChannel_SPIM.hpp"
//#include <coco/convert.hpp>
//#include <coco/debug.hpp>


namespace coco {

SpiDisplayChannel_SPIM::SpiDisplayChannel_SPIM(SpiMaster_SPIM &device, gpio::Config csPin,
    gpio::Config commandPin, bool commandPinShared, uint8_t commandMask, spim::Format format)
    : SpiMaster_SPIM::Channel(device, csPin, format)
    , commandPin_(commandPin), commandPinShared_(commandPinShared), commandMask_(commandMask)
{
    gpio::enableOutput(commandPin, false);
}

SpiDisplayChannel_SPIM::~SpiDisplayChannel_SPIM() {
}

int SpiDisplayChannel_SPIM::transferFirst(SpiMaster_SPIM::BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // get command indicator form first byte of header
    // note that the header size must be at least 1 byte, otherwise the assertion fails
    bool command = (buffer.header<uint8_t>() & commandMask_) != 0;

    // set command pin
    gpio::setOutput(commandPin_, command);

    // disconnect MISO pin if command pin is shared with MISO pin
    if (commandPinShared_)
        r.spi->PSEL.MISO = r.spi->PSEL.MISO | N(SPIM_PSEL_MISO_CONNECT, Disconnected);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // start transfer of buffer data
    start(buffer.op(), buffer.data(), buffer.size());

    return 1;
    // -> SPIM_IRQHandler()
}

int SpiDisplayChannel_SPIM::transferNext(SpiMaster_SPIM::BufferBase &buffer, int steps) {
    auto &r = registers();

    // deactivate CS pin
    gpio::setOutput(csPin_, false);

    // reconnect MISO pin if command pin is shared with MISO pin
    if (commandPinShared_)
        r.spi->PSEL.MISO = r.spi->PSEL.MISO & ~SPIM_PSEL_MISO_CONNECT_Msk;

    // indicate finished
    return 0;//true
    // -> SPIM_IRQHandler()
}

} // namespace coco
