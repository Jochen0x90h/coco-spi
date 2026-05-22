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

        /*transfers_.pop(
            [this](BufferBase &buffer) {
                // try to start the next transfer
                int steps = buffer.channel_.transferNext(buffer, buffer.steps_);
                if (steps == 0) {
                    // notify app that buffer has finished
                    loop_.push(buffer);
                    return true;
                }
                buffer.steps_ = steps;

                // more transfers needed
                return false;
            },
            [](BufferBase &next) {
                // start next buffer
                next.steps_ = next.channel_.transferFirst(next);
            }
        );*/
        auto buffer = transfers_.popIf(
            [this](auto &buffer) {
                // try to start the next transfer
                int steps = buffer.channel_.transferNext(buffer, buffer.steps_);
                buffer.steps_ = steps;

                // transfer is finished when steps has reached 0
                return steps == 0;
            },
            [](auto &next) {
                // start next buffer
                next.steps_ = next.channel_.transferFirst(next);
            }/*,
            [this](auto &buffer) {
                // notify app that buffer has finished
                loop_.push(buffer);
            }*/);
        if (buffer != nullptr) {
            // notify app that buffer has finished
            loop_.push(*buffer);
        }
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

int SpiMaster_SPIM::Channel::transferFirst(BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    int size = buffer.headerCapacity_;//std::min(uint16_t(buffer.headerType_), buffer.headerCapacity_);
    if (size == 0) {
        // no header, start transfer of buffer data
        start(buffer.op(), buffer.data(), buffer.size());
        //buffer.setOp(BufferBase::Op::NONE);

        // one more step to do (disable CS pin)
        return 1;
    } else {
        // start transfer of header
        start(BufferBase::Op::WRITE, buffer.header_, size);

        // two more steps to do (transfer data, disable CS pin)
        return 2;
    }
    // -> SPIM_IRQHandler()
}

int SpiMaster_SPIM::Channel::transferNext(BufferBase &buffer, int steps) {
    auto &r = registers();

    auto op = buffer.op() & BufferBase::Op::READ_WRITE;
    if (op == BufferBase::Op::NONE) {
        // deactivate CS pin
        gpio::setOutput(csPin_, false);

        // indicate finished
        return 0;//true;
    }
    //buffer.setOp(BufferBase::Op::NONE);

    // set buffer data
    volatile void *data = buffer.data();
    int size = buffer.size();
    r.spi.setRxData(data, (op & BufferBase::Op::READ) != 0 ? size : 0);
    r.spi.setTxData(data, (op & BufferBase::Op::WRITE) != 0 ? size : 0);

    // start transfer
    r.spi.start();

    return 1;//false;
    // -> SPIM_IRQHandler()
}


// SpiMaster_SPIM::BufferBase

SpiMaster_SPIM::BufferBase::BufferBase(uint8_t *header, int headerCapacity, uint8_t* data, int capacity, Channel &channel)
    : coco::Buffer(header, headerCapacity, data, capacity, BufferBase::State::READY), channel_(channel)
{
    channel.buffers_.add(*this);
}

SpiMaster_SPIM::BufferBase::~BufferBase() {
}

bool SpiMaster_SPIM::BufferBase::start() {
    if (state_ != State::READY) {
        assert(false);
        setError(std::errc::resource_unavailable_try_again);
        return false;
    }
    if (((op_ & Op::READ_WRITE) == 0 || size_ == 0) && ((op_ & Op::ERASE) == 0 || !channel_.eraseSupported_)) {
        setSuccess();
        return false;
    }
    //debug::out << "BufferBase::start\n";

    //op_ = op;
    auto &channel = channel_;
    auto &device = channel.device_;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers_.guardedPush(nvic::Guard(device.spiIrq_), *this))
        steps_ = channel.transferFirst(*this);

    // set state
    setBusy();

    return true;
}

bool SpiMaster_SPIM::BufferBase::cancel() {
    if (state_ != State::BUSY)
        return false;
    auto &device = channel_.device_;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers_.guardedRemoveExceptFirst(nvic::Guard(device.spiIrq_), *this)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt is enabled at this point
        setError(std::errc::operation_canceled);
        setReady();
    }

    return true;
}

void SpiMaster_SPIM::BufferBase::onCompletion() {
    setSuccess();
    setReady();
}

} // namespace coco
