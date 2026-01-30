#include "SpiMaster_SPIM.hpp"
//#include <coco/convert.hpp>
//#include <coco/debug.hpp>


namespace coco {

// SpiMaster_SPIM

SpiMaster_SPIM::SpiMaster_SPIM(Loop_Queue &loop, gpio::Config sckPin, gpio::Config mosiPin, gpio::Config misoPin,
    const spim::Info &spiInfo)
    : loop_(loop)
{
    spiInfo.enablePins(sckPin, mosiPin, misoPin);

    auto &r = registers_;

    // configure SPI
    // permanently enable SPI to ensure the right idle level for the clock
    // (Does not start until TXFIFO gets written or TX DMA enabled)
    r.spi = spiInfo.instance()
        .enable(
            spim::Format::DEFAULT,
            spim::Interrupt::END);

    // setup IRQ for RX DMA channel (gets enabled on first call to BufferBase::start())
    spiIrq_ = spiInfo.irq;
    nvic::setPriority(spiIrq_, nvic::Priority::MEDIUM);

    // clear interrupt flags
    r.spi->EVENTS_END = 0;
    nvic::clear(spiIrq_);
}

void SpiMaster_SPIM::SPIM_IRQHandler() {
    // check if read DMA has completed
    auto &r = registers_;

    if (r.spi->EVENTS_END) {
        r.spi->EVENTS_END = 0;

        // todo: handle partial transfers (BufferBase::Op::PARTIAL flag set)

        transfers_.pop(
            [this](BufferBase &buffer) {
                // try to start the next transfer
                if (buffer.channel_.transferNext(buffer)) {
                    // notify app that buffer has finished
                    loop_.push(buffer);
                    return true;
                }

                // more transfers needed
                return false;
            },
            [](BufferBase &next) {
                // start next buffer
                next.channel_.transferFirst(next);
            }
        );
    }
}


// SpiMaster_SPIM::Channel

SpiMaster_SPIM::Channel::Channel(SpiMaster_SPIM &device, gpio::Config csPin, spim::Format format)
    : BufferDevice(State::READY)
    , device_(device), csPin_(csPin), format_(format)
{
    // configure CS pin
    gpio::enableOutput(csPin, false);
}

SpiMaster_SPIM::Channel::~Channel() {
}

int SpiMaster_SPIM::Channel::getBufferCount() {
    return buffers_.count();
}

SpiMaster_SPIM::BufferBase &SpiMaster_SPIM::Channel::getBuffer(int index) {
    return buffers_.get(index);
}

void SpiMaster_SPIM::Channel::transferFirst(BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    int size = std::min(uint16_t(buffer.headerType_), buffer.headerCapacity_);
    if (size == 0) {
        // no header, start transfer of buffer data
        start(buffer.op(), buffer.data(), buffer.size());
        buffer.setOp(BufferBase::Op::NONE);
    } else {
        // start transfer of header
        start(BufferBase::Op::WRITE, buffer.header_, size);
    }

    // -> SPIM_IRQHandler()
}

bool SpiMaster_SPIM::Channel::transferNext(BufferBase &buffer) {
    auto &r = registers();

    auto op = buffer.op() & BufferBase::Op::READ_WRITE;
    if (op == BufferBase::Op::NONE) {
        // deactivate CS pin
        gpio::setOutput(csPin_, false);

        // indicate finished
        return true;
    }
    buffer.setOp(BufferBase::Op::NONE);

    // set buffer data
    volatile void *data = buffer.data();
    int size = buffer.size();
    r.spi.setRxData(data, (op & BufferBase::Op::READ) != 0 ? size : 0);
    r.spi.setTxData(data, (op & BufferBase::Op::WRITE) != 0 ? size : 0);

    // start transfer
    r.spi.start();

    // -> DMAx_Rx_IRQHandler()
    return false;
}


// SpiMaster_SPIM::BufferBase

SpiMaster_SPIM::BufferBase::BufferBase(uint8_t *header, int headerCapacity, uint8_t* data, int capacity, Channel &channel)
    : coco::Buffer(header, headerCapacity, headerCapacity, data, capacity, BufferBase::State::READY), channel_(channel)
{
    channel.buffers_.add(*this);
}

SpiMaster_SPIM::BufferBase::~BufferBase() {
}

bool SpiMaster_SPIM::BufferBase::start(Op op) {
    if (st.state != State::READY || (((op & Op::READ_WRITE) == 0 || size_ == 0) && ((op & Op::ERASE) == 0 || !channel_.eraseSupported_))) {
        // starting a buffer when the state is BUSY is a bug
        assert(st.state != State::BUSY);
        return false;
    }
    //debug::out << "BufferBase::start\n";

    op_ = op;
    auto &channel = channel_;
    auto &device = channel.device_;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers_.push(nvic::Guard(device.spiIrq_), *this))
        channel.transferFirst(*this);

    // set state
    setBusy();

    return true;
}

bool SpiMaster_SPIM::BufferBase::cancel() {
    if (st.state != State::BUSY)
        return false;
    auto &device = channel_.device_;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers_.remove(nvic::Guard(device.spiIrq_), *this, false)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt should be enabled at this point
        setReady(0);
    }

    return true;
}

void SpiMaster_SPIM::BufferBase::handle() {
    setReady();
}

} // namespace coco
