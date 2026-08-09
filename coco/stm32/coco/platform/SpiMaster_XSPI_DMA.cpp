#include "SpiMaster_XSPI_DMA.hpp"
//#include <coco/convert.hpp>
//#include <coco/debug.hpp>


#ifdef HAVE_XSPI
namespace coco {

// SpiMaster_XSPI_DMA

SpiMaster_XSPI_DMA::SpiMaster_XSPI_DMA(Loop_Queue &loop, const xspi::Info &xspiInfo,
    Array<const gpio::Config> pins, const dma::Info<> &dmaInfo)
    : loop_(loop)
{
    // configure pins
    for (auto pin : pins) {
        gpio::enableAlternate(pin);
    }

    auto &r = registers_;

    // configure QUADSPI
    // permanently enable SPI to ensure the right idle level for the clock
    // (Does not start until TXFIFO gets written or TX DMA enabled)
    r.xspi = xspiInfo.enableClock()
        .enable(xspi::Format::NONE, 0,
            xspi::Interrupt::TRANSFER_COMPLETE,
            xspi::DmaRequest::RX_TX);
    xspiIrq_= xspiInfo.irq;
    nvic::setPriority(xspiIrq_, nvic::Priority::MEDIUM);

    // configure DMA channel (same channel is used for RX and TX, therefore it can't be pre-configured)
    r.dma.rx = dmaInfo.enableClock<RxChannel::MODE>();

    // map DMA to QUADSPI
    xspiInfo.map(dmaInfo);
}

void SpiMaster_XSPI_DMA::XSPI_IRQHandler() {
    //debug::out << "irq\n";
    auto &r = registers_;

    // check if transfer has completed
    auto status = r.xspi.status();
    if ((status & xspi::Status::TRANSFER_COMPLETE) != 0) {
        // clear interrupt flag
        r.xspi.clear(xspi::Status::TRANSFER_COMPLETE);

        r.dma.rx.clear(dma::Status::TRANSFER_COMPLETE);

        // disable DMA
        r.dma.rx.disable();

        // end of transfer
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
                next.channel_.transferFirst(next);
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


// SpiMaster_XSPI_DMA::BufferBase

SpiMaster_XSPI_DMA::BufferBase::BufferBase(uint8_t *headerAndData, int capacity, Channel &channel)
    : coco::Buffer(headerAndData, 4, capacity, BufferBase::State::READY), channel_(channel)
{
    channel.buffers_.add(*this);
}

SpiMaster_XSPI_DMA::BufferBase::~BufferBase() {
}

bool SpiMaster_XSPI_DMA::BufferBase::start() {
    if (state_ != State::READY) {
        assert(false);
        setError(std::errc::resource_unavailable_try_again);
        return false;
    }
    if (((op_ & Op::READ_WRITE) == 0 || size_ == 0) && ((op_ & Op::ERASE) == 0)) {
        setSuccess();
        return false;
    }

    //op_ = op;
    auto &channel = channel_;
    auto &device = channel.device_;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers_.guardedPush(nvic::Guard(device.xspiIrq_), *this))
        steps_ = channel.transferFirst(*this);

    // set state
    setBusy();

    return true;
}

bool SpiMaster_XSPI_DMA::BufferBase::cancel() {
    if (state_ != State::BUSY)
        return false;
    auto &device = channel_.device_;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers_.guardedRemoveExceptFirst(nvic::Guard(device.xspiIrq_), *this)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt is enabled at this point
        setError(std::errc::operation_canceled);
        setReady();
    }

    return true;
}

void SpiMaster_XSPI_DMA::BufferBase::onCompletion() {
    setReady();
}


// SpiMaster_XSPI_DMA::Channel

SpiMaster_XSPI_DMA::Channel::Channel(SpiMaster_XSPI_DMA &device, gpio::Config csPin, xspi::Format format)
    : BufferDevice(State::READY), device_(device), csPin_(csPin), format_(format)
{
    // configure CS pin
    gpio::enableOutput(csPin, false);
}

SpiMaster_XSPI_DMA::Channel::~Channel() {
}

int SpiMaster_XSPI_DMA::Channel::getBufferCount() {
    return buffers_.count();
}

SpiMaster_XSPI_DMA::BufferBase &SpiMaster_XSPI_DMA::Channel::getBuffer(int index) {
    return buffers_.get(index);
}


// SpiMaster_XSPI_DMA::RegistersChannel

SpiMaster_XSPI_DMA::RegistersChannel::~RegistersChannel() {
}

int SpiMaster_XSPI_DMA::RegistersChannel::transferFirst(BufferBase &buffer) {
    auto &r = registers();

    // wait until QUADSQI is ready
    while ((r.xspi.status() & xspi::Status::BUSY) != 0);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // set format (clock speed, bank and memory size)
    r.xspi.setFormat(format_);

    uint32_t address = buffer.header<uint32_t>();
    volatile uint8_t *data = buffer.data();
    //data[3] = 0;
    //debug::out << hex(data[3]) << '\n';
    int size = buffer.size();

    if ((buffer.op() & BufferBase::Op::WRITE) == 0) {
        // read
        //debug::out << "read\n";
        r.xspi
            .setSize(size)
            .setCommConfig(xspi::Function::INDIRECT_READ, readCommConfig_)
            .setAddress(address);
        r.dma.rx.configure()
            .setSourceAddress(&r.xspi->DR)
            .setDestinationAddress(data)
            .setCount(size)
            .enable();

    } else {
        // write
        r.xspi
            .setSize(size)
            .setCommConfig(xspi::Function::INDIRECT_WRITE, writeCommConfig_)
            .setAddress(address);
        r.dma.tx.configure()
            .setSourceAddress(data)
            .setDestinationAddress(&r.xspi->DR)
            .setCount(size)
            .enable();
    }

    // one more step to do (disable CS pin)
    return 1;

    // -> QUADSPI_IRQHandler
}

int SpiMaster_XSPI_DMA::RegistersChannel::transferNext(BufferBase &buffer, int steps) {
    auto &r = registers();


    // deactivate CS pin
    gpio::setOutput(csPin_, false);
    while ((r.xspi.status() & xspi::Status::BUSY) != 0);

    volatile uint8_t *data = buffer.data();
    //debug::out << hex(data[3]) << '\n';
    //debug::out << dec(r.dma.rx.count()) << '\n';

    // indicate finished
    return 0;
}


// SpiMaster_XSPI_DMA::MemoryChannel

SpiMaster_XSPI_DMA::MemoryChannel::~MemoryChannel() {
}

int SpiMaster_XSPI_DMA::MemoryChannel::transferFirst(BufferBase &buffer) {
    auto &r = registers();

    // wait until QUADSQI is ready
    while ((r.xspi.status() & xspi::Status::BUSY) != 0);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // set format (clock speed, bank and memory size)
    r.xspi.setFormat(format_);

    auto op = buffer.op() & (BufferBase::Op::WRITE | BufferBase::Op::ERASE);
    if (op == BufferBase::Op::NONE) {
        // read
        volatile void *data = buffer.data();
        int size = buffer.size();

        // configure QUADSPI for read
        uint32_t address = buffer.header<uint32_t>();
        r.xspi
            .setSize(size)
            .setCommConfig(xspi::Function::INDIRECT_READ, readCommConfig_)
            .setAddress(address);

        // configure DMA for read
        r.dma.rx.configure()
            .setSourceAddress(&r.xspi->DR)
            .setDestinationAddress(data)
            .setCount(size)
            .enable();

        // one more step to do (disable CS pin)
        return 1;
    } else {
        // write or erase: send write enable command
        r.xspi.setCommConfig(xspi::Function::INDIRECT_WRITE, writeEnableCommConfig_);

        // set write in progress bit
        //status_ = 1;

        // three more steps to do (write or erase, read status, disable CS pin)
        return 3;
    }
    // -> QUADSPI_IRQHandler
}

int SpiMaster_XSPI_DMA::MemoryChannel::transferNext(BufferBase &buffer, int steps) {
    auto &r = registers();

    // deactivate CS pin
    gpio::setOutput(csPin_, false);

    // wait until QUADSQI is not busy
    while ((r.xspi.status() & xspi::Status::BUSY) != 0);

    if (steps == 1) {
        // indicate finished
        return 0;
    }

    if (steps == 3) {
        // write or erase
        auto op = buffer.op();
        volatile void *data = buffer.data();
        int size = buffer.size();
        if ((op & BufferBase::Op::ERASE) == 0) {
            // write
            r.xspi
                .setSize(size)
                .setCommConfig(xspi::Function::INDIRECT_WRITE, writeCommConfig_);
        } else {
            // erase
            r.xspi
                .setCommConfig(xspi::Function::INDIRECT_WRITE, eraseCommConfig_);
        }

        // configure QUADSPI, activate CS pin
        uint32_t address = buffer.header<uint32_t>();
        gpio::setOutput(csPin_, true);
        r.xspi.setAddress(address); // earliest point where QUADSPI starts

        if ((op & BufferBase::Op::ERASE) == 0) {
            // configure DMA for write
            r.dma.tx.configure()
                .setSourceAddress(data)
                .setDestinationAddress(&r.xspi->DR)
                .setCount(size)
                .enable();
        } else {
            // erase: no data needed
        }

        // indicate write or erase in progress
        status_ = 1;

        // two more steps to do (read status, disable CS pin)
        return 2;
    } else {
        // read status
        if ((status_ & 1) == 1) {
            r.xspi.setSize(1);

            gpio::setOutput(csPin_, true);

            r.xspi.setCommConfig(xspi::Function::INDIRECT_READ, readStatusCommConfig_);

            r.dma.rx.configure()
                .setSourceAddress(&r.xspi->DR)
                .setDestinationAddress(&status_)
                .setCount(1)
                .enable();

            // two more steps to do (read status again, disable CS pin)
            return 2;
        }

        // indicate finished
        return 0;
    }
}

} // namespace coco
#endif // HAVE_XSPI
