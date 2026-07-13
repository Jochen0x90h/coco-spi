#include "SpiDisplayChannel_SPI_DMA.hpp"
//#include <coco/convert.hpp>
//#include <coco/debug.hpp>


namespace coco {

SpiDisplayChannel_SPI_DMA::SpiDisplayChannel_SPI_DMA(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format,
    gpio::Config commandPin, bool commandPinShared, uint8_t commandMask)
    : SpiMaster_SPI_DMA::Channel(device, csPin, format)
    , commandPin_(commandPin), commandPinShared_(commandPinShared), commandMask_(commandMask)
{
    if (!commandPinShared) {
        // configure command pin
        gpio::enableOutput(commandPin, false);
    }
}

SpiDisplayChannel_SPI_DMA::~SpiDisplayChannel_SPI_DMA() {
}

int SpiDisplayChannel_SPI_DMA::transferFirst(SpiMaster_SPI_DMA::BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // start SPI (needed for newer versions to indicate that no transfer size is used)
    r.spi.start();

    // get command indicator form first byte of header
    // note that the header size must be at least 1 byte, otherwise the assertion fails
    bool command = (buffer.header<uint8_t>() & commandMask_) != 0;

    // set mode to output if command pin is shared with MISO pin
    if (commandPinShared_)
        gpio::setMode(commandPin_, gpio::Mode::OUTPUT);

    // set command pin
    gpio::setOutput(commandPin_, command);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // start transfer of buffer data
    start(buffer.op(), buffer.data(), buffer.size());

    // one more step to do (disable CS pin)
    return 1;
    // -> DMAx_Rx_IRQHandler()
}

int SpiDisplayChannel_SPI_DMA::transferNext(SpiMaster_SPI_DMA::BufferBase &buffer, int steps) {
    // deactivate CS pin
    gpio::setOutput(csPin_, false);

    // set mode to alternate if command pin is shared with MISO pin
    if (commandPinShared_)
        gpio::setMode(commandPin_, gpio::Mode::ALTERNATE);

    // indicate finished
    return 0;//true;
    // -> DMAx_Rx_IRQHandler()
}

} // namespace coco
