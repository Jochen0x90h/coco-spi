#include "SpiMemory_QUADSPI_DMA.hpp"
#include <coco/convert.hpp>
#include <coco/debug.hpp>


#ifdef HAVE_QUADSPI
namespace coco {

// SpiMemory_QUADSPI_DMA

SpiMemory_QUADSPI_DMA::SpiMemory_QUADSPI_DMA(Loop_Queue &loop, Array<const gpio::Config> pins,
    const qspi::Info &qspiInfo, const dma::Info<> &dmaInfo)
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
    r.qspi = qspiInfo.enableClock()
        .enable(qspi::Format::NONE, 0,
            qspi::Interrupt::TRANSFER_COMPLETE,
            qspi::DmaRequest::RX_TX);
    qspiIrq_= qspiInfo.irq;
    nvic::setPriority(qspiIrq_, nvic::Priority::MEDIUM);

    // configure DMA channel (same channel is used for RX and TX, therefore it can't be pre-configured)
    r.dma.rx = dmaInfo.enableClock<RxChannel::MODE>();

    // map DMA to QUADSPI
    qspiInfo.map(dmaInfo);
}

void SpiMemory_QUADSPI_DMA::QUADSPI_IRQHandler() {
    //debug::out << "irq\n";
    auto &r = registers_;

    // check if transfer has completed
    auto status = r.qspi.status();
    if ((status & qspi::Status::TRANSFER_COMPLETE) != 0) {
        // clear interrupt flag
        r.qspi.clear(qspi::Status::TRANSFER_COMPLETE);

        r.dma.rx.clear(dma::Status::TRANSFER_COMPLETE);

        // disable DMA
        r.dma.rx.disable();

        // end of transfer
        transfers_.pop(
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
        );
    }
}


// SpiMemory_QUADSPI_DMA::BufferBase

SpiMemory_QUADSPI_DMA::BufferBase::BufferBase(uint8_t *headerAndData, int capacity, Channel &channel)
    : coco::Buffer(headerAndData, 4, 0, capacity, BufferBase::State::READY), channel_(channel)
{
    channel.buffers_.add(*this);
}

SpiMemory_QUADSPI_DMA::BufferBase::~BufferBase() {
}

bool SpiMemory_QUADSPI_DMA::BufferBase::start() {
    if (state_ != State::READY || (((op_ & Op::READ_WRITE) == 0 || size_ == 0) && ((op_ & Op::ERASE) == 0))) {
        // starting a buffer when the state is BUSY is a bug
        assert(st.state != State::BUSY);
        return false;
    }

    //op_ = op;
    auto &channel = channel_;
    auto &device = channel.device_;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers_.push(nvic::Guard(device.qspiIrq_), *this))
        steps_ = channel.transferFirst(*this);

    // set state
    setBusy();

    return true;
}

bool SpiMemory_QUADSPI_DMA::BufferBase::cancel() {
    if (state_ != State::BUSY)
        return false;
    auto &device = channel_.device_;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers_.remove(nvic::Guard(device.qspiIrq_), *this, false)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt is enabled at this point
        setError(std::errc::operation_canceled);
        setReady();
    }

    return true;
}

void SpiMemory_QUADSPI_DMA::BufferBase::handle() {
    setReady();
}


// SpiMemory_QUADSPI_DMA::Channel

SpiMemory_QUADSPI_DMA::Channel::Channel(SpiMemory_QUADSPI_DMA &device, gpio::Config csPin,
    qspi::Format format, qspi::CommFormat commFormat,
    uint8_t readCommand, uint8_t writeEnableCommand, uint8_t writeCommand, uint8_t eraseCommand, uint8_t readStatusCommand)
    : BufferDevice(State::READY)
    , device_(device), csPin_(csPin)
    , format_(format), commFormat_(commFormat)
    , readCommand_(readCommand), writeEnableCommand_(writeEnableCommand), writeCommand_(writeCommand), eraseCommand_(eraseCommand), readStatusCommand_(readStatusCommand)
{
    // configure CS pin
    gpio::enableOutput(csPin, false);
}

SpiMemory_QUADSPI_DMA::Channel::~Channel() {
}

int SpiMemory_QUADSPI_DMA::Channel::getBufferCount() {
    return buffers_.count();
}

SpiMemory_QUADSPI_DMA::BufferBase &SpiMemory_QUADSPI_DMA::Channel::getBuffer(int index) {
    return buffers_.get(index);
}

int SpiMemory_QUADSPI_DMA::Channel::transferFirst(BufferBase &buffer) {
    auto &r = registers();

    // wait until QUADSQI is not busy
    while ((r.qspi.status() & qspi::Status::BUSY) != 0);

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // set format (clock speed, bank and memory size)
    r.qspi.setFormat(format_);

    auto op = buffer.op() & (BufferBase::Op::WRITE | BufferBase::Op::ERASE);
    if (op == BufferBase::Op::NONE) {
        // read
        volatile void *data = buffer.data();
        int size = buffer.size();

        // configure QUADSPI for read
        uint32_t address = buffer.header<uint32_t>();
        r.qspi
            .setSize(size)
            .setCommConfig(commFormat_, qspi::Function::INDIRECT_READ, readCommand_)
            .setAddress(address);

        // configure DMA for read
        r.dma.rx.configure()
            .setSourceAddress(&r.qspi->DR)
            .setDestinationAddress(data)
            .setCount(size)
            .enable();

        // one more step to do (disable CS pin)
        return 1;
    } else {
        // write or erase: send write enable command
        r.qspi.setCommConfig(qspi::CommFormat::INSTRUCTION_1_LINE, qspi::Function::INDIRECT_WRITE, writeEnableCommand_);

        // set write in progress bit
        //status_ = 1;

        // three more steps to do (write or erase, read status, disable CS pin)
        return 3;
    }
    // -> QUADSPI_IRQHandler
}

int SpiMemory_QUADSPI_DMA::Channel::transferNext(BufferBase &buffer, int steps) {
    auto &r = registers();

    // deactivate CS pin
    gpio::setOutput(csPin_, false);

    // wait until QUADSQI is not busy
    while ((r.qspi.status() & qspi::Status::BUSY) != 0);

    if (steps == 1) {
        // indicate finished
        return 0;
    }

    if (steps == 3) {
        // write or erase
        auto op = buffer.op();
        volatile void *data = buffer.data();
        int size = buffer.size();
        auto commFormat = commFormat_;
        uint8_t command;
        if ((op & BufferBase::Op::ERASE) == 0) {
            // write
            command = writeCommand_;
            r.qspi.setSize(size);
        } else {
            // erase
            commFormat &= ~qspi::CommFormat::DATA_MASK;
            command = eraseCommand_;
        }

        // configure QUADSPI, activate CS pin
        uint32_t address = buffer.header<uint32_t>();
        r.qspi
            .setCommConfig(commFormat, qspi::Function::INDIRECT_WRITE, command);
        gpio::setOutput(csPin_, true);
        r.qspi.setAddress(address); // earliest point where QUADSPI starts

        if ((op & BufferBase::Op::ERASE) == 0) {
            // write
            r.dma.tx.configure()
                .setSourceAddress(data)
                .setDestinationAddress(&r.qspi->DR)
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
            r.qspi.setSize(1);

            gpio::setOutput(csPin_, true);

            r.qspi.setCommConfig(qspi::CommFormat::INSTRUCTION_1_LINE | qspi::CommFormat::DATA_1_LINE,
                qspi::Function::INDIRECT_READ, readStatusCommand_);

            r.dma.rx.configure()
                .setSourceAddress(&r.qspi->DR)
                .setDestinationAddress(&status_)
                .setCount(1)
                .enable();

            // two more steps to do (read status again, disable CS pin)
            return 2;
        }

        // indicate finished
        return 0;
    }

/*
    auto op = buffer.op() & (BufferBase::Op::WRITE | BufferBase::Op::ERASE);
    if (op == BufferBase::Op::NONE) {
        // write or erase: read status
        if ((status_ & 1) == 1) {
            r.qspi.setSize(1);

            gpio::setOutput(csPin_, true);

            r.qspi.setCommConfig(qspi::CommFormat::INSTRUCTION_1_LINE | qspi::CommFormat::DATA_1_LINE,
                qspi::Function::INDIRECT_READ, readStatusCommand_);

            r.dma.rx.configure()
                .setSourceAddress(&r.qspi->DR)
                .setDestinationAddress(&status_)
                .setCount(1)
                .enable();

            // not finished yet
            return false;
        }

        // indicate finished
        return true;
    }
    buffer.setOp(BufferBase::Op::NONE);

    volatile void *data = buffer.data();
    int size = buffer.size();
    auto commFormat = commFormat_;
    uint8_t command;
    if ((op & BufferBase::Op::ERASE) == 0) {
        // write
        command = writeCommand_;
        r.qspi.setSize(size);
    } else {
        // erase
        commFormat &= ~qspi::CommFormat::DATA_MASK;
        command = eraseCommand_;
    }

    // configure QUADSPI, activate CS pin
    uint32_t address = buffer.header<uint32_t>();
    r.qspi
        .setCommConfig(commFormat, qspi::Function::INDIRECT_WRITE, command);
    gpio::setOutput(csPin_, true);
    r.qspi.setAddress(address); // earliest point where QUADSPI starts

    if ((op & BufferBase::Op::ERASE) == 0) {
        // write
        r.dma.tx.configure()
            .setSourceAddress(data)
            .setDestinationAddress(&r.qspi->DR)
            .setCount(size)
            .enable();
    } else {
        // erase: no data needed
    }

    // not finished yet
    return false;
    */
}

} // namespace coco
#endif // HAVE_QUADSPI
