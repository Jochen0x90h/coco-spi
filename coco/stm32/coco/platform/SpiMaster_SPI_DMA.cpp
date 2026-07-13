#include "SpiMaster_SPI_DMA.hpp"
//#include <coco/convert.hpp>
//#include <coco/debug.hpp>


namespace coco {

// SpiMaster_SPI_DMA

SpiMaster_SPI_DMA::SpiMaster_SPI_DMA(Loop_Queue &loop, const spi::Info &spiInfo, gpio::Config sckPin,
    gpio::Config mosiPin, gpio::Config misoPin, const dma::DualInfo<> &dmaInfo, spi::Config config)
    : loop_(loop)
{
    // configure pins
    spiInfo.enablePins(sckPin, mosiPin, misoPin);

    auto &r = registers_;

    // configure SPI
    // permanently enable SPI to ensure the right idle level for the clock
    // (Does not start until TXFIFO gets written or TX DMA enabled)
    auto spi = r.spi = spiInfo.enableClock()
        .enable(config,
            spi::Format::DEFAULT,
            spi::Interrupt::NONE,
            spi::DmaRequest::RX_TX);

    // configure DMA channels
    auto [rxChannel, txChannel] = dmaInfo.enableClock<RxChannel::MODE, TxChannel::MODE>();
    r.dma.rx = rxChannel
        .setSourceAddress(&spi.RXDR8());
    r.dma.tx = txChannel
        .configure()
        .setDestinationAddress(&spi.TXDR8());

    // setup IRQ for RX DMA channel (gets enabled on first call to BufferBase::start())
    rxDmaIrq_ = dmaInfo.irq1;
    nvic::setPriority(rxDmaIrq_, nvic::Priority::MEDIUM);

    // map DMA to SPI
    spiInfo.map(dmaInfo);

    // clear interrupt flags
    spi.clear(spi::Status::ALL);
    nvic::clear(rxDmaIrq_);
}

void SpiMaster_SPI_DMA::DMA_Rx_IRQHandler() {
    auto &r = registers_;

    // check if read DMA has completed
    if ((r.dma.rx.status() & dma::Status::TRANSFER_COMPLETE) != 0) {
        // clear interrupt flag
        r.dma.rx.clear(dma::Status::TRANSFER_COMPLETE);

        // also clear tx flag, needed on STM32F4
        r.dma.tx.clear(dma::Status::TRANSFER_COMPLETE);

        // disable DMA
        r.dma.rx.disable();
        r.dma.tx.disable();

        // todo: handle partial transfers (BufferBase::Op::PARTIAL flag set)

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


// SpiMaster_SPI_DMA::BufferBase

SpiMaster_SPI_DMA::BufferBase::BufferBase(uint8_t *header, int headerCapacity, uint8_t* data, int capacity, Channel &channel)
    : coco::Buffer(header, headerCapacity, data, capacity, BufferBase::State::READY), channel_(channel)
{
    channel.buffers_.add(*this);
}

SpiMaster_SPI_DMA::BufferBase::~BufferBase() {
}

bool SpiMaster_SPI_DMA::BufferBase::start() {
    if (state_ != State::READY) {
        assert(false);
        setError(std::errc::resource_unavailable_try_again);
        return false;
    }
    if (((op_ & Op::READ_WRITE) == 0 || size_ == 0) && ((op_ & Op::ERASE) == 0 || (channel_.flags_ & Flags::SUPPORT_ERASE) == 0)) {
        setSuccess();
        return false;
    }
    //debug::out << "BufferBase::start\n";

    //op_ = op;
    auto &channel = channel_;
    auto &device = channel.device_;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers_.guardedPush(nvic::Guard(device.rxDmaIrq_), *this))
        steps_ = channel.transferFirst(*this);

    // set state
    setBusy();

    return true;
}

bool SpiMaster_SPI_DMA::BufferBase::cancel() {
    if (state_ != State::BUSY)
        return false;
    auto &device = channel_.device_;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers_.guardedRemoveExceptFirst(nvic::Guard(device.rxDmaIrq_), *this)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt is enabled at this point
        setError(std::errc::operation_canceled);
        setReady();
    }

    return true;
}

void SpiMaster_SPI_DMA::BufferBase::onCompletion() {
    setReady();
}


// SpiMaster_SPI_DMA::Channel

SpiMaster_SPI_DMA::Channel::Channel(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format, Flags flags)
    : BufferDevice(State::READY)
    , device_(device), csPin_(csPin), format_(format), flags_(flags)
{
    // configure CS pin
    gpio::enableOutput(csPin, false);
}

SpiMaster_SPI_DMA::Channel::~Channel() {
}

int SpiMaster_SPI_DMA::Channel::getBufferCount() {
    return buffers_.count();
}

SpiMaster_SPI_DMA::BufferBase &SpiMaster_SPI_DMA::Channel::getBuffer(int index) {
    return buffers_.get(index);
}

int SpiMaster_SPI_DMA::Channel::transferFirst(BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // start SPI (needed for newer versions to indicate that no transfer size is used)
    r.spi.start();

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // get header
    auto header = buffer.header_;
    int size = buffer.headerCapacity_;

    // check for variable header size
    if ((flags_ & Flags::VARIABLE_HEADER_SIZE) != 0) {
        size = header[0];
        ++header;
    }

    if (size == 0) {
        // no header, start transfer of buffer data
        start(buffer.op(), buffer.data(), buffer.size());

        // one more step to do (disable CS pin)
        return 1;
    } else {
        // start transfer of header
        //debug::out << "start header size " << dec(size) << '\n';
        start(BufferBase::Op::WRITE, header, size);

        // two more steps to do (transfer data, disable CS pin)
        return 2;
    }
    // -> DMAx_Rx_IRQHandler()
}

int SpiMaster_SPI_DMA::Channel::transferNext(BufferBase &buffer, int steps) {
    if (steps == 1) {
        // deactivate CS pin
        //debug::out << "deactivate CS\n";
        gpio::setOutput(csPin_, false);

        // no more steps to do
        return 0;
    }

    // start transfer of buffer data
    //debug::out << "start data size " << dec(buffer.size()) << '\n';
    start(buffer.op(), buffer.data(), buffer.size());

    // one more step to do (disable CS pin)
    return 1;
    // -> DMAx_Rx_IRQHandler()
}

void SpiMaster_SPI_DMA::Channel::start(BufferBase::Op op, volatile void *data, int size) {
    auto &r = registers();

    if (op == BufferBase::Op::WRITE) {
        // start DMA for write only
        r.dma.rx
            .configure(0, dma::Destination::NO_INCREMENT) // don't increment memory
            .setDestinationAddress(&dummy_); // read into dummy
    } else {
        // start DMA for write and read
        r.dma.rx
            .configure(0, dma::Destination::INCREMENT) // increment memory
            .setDestinationAddress(data); // read into memory
    }
    r.dma.rx
        .setCount(size)
        .enable(dma::Config::TRANSFER_COMPLETE_INTERRUPT);
    r.dma.tx
        .setSourceAddress(data)
        .setCount(size)
        .enable();
}


// SpiMaster_SPI_DMA::ByteRegistersChannel

SpiMaster_SPI_DMA::ByteRegistersChannel::~ByteRegistersChannel() {
}

int SpiMaster_SPI_DMA::ByteRegistersChannel::transferFirst(SpiMaster_SPI_DMA::BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // start SPI (needed for newer versions to indicate that no transfer size is used)
    r.spi.start();

    // build header: instruction + address (max 7 bytes)
    int addressMask = addressMask_;
    int addressMult = firstBit(addressMask);
    uint8_t address = (buffer.header<uint32_t>() * addressMult) & addressMask;
    if ((buffer.op() & BufferBase::Op::WRITE) == 0) {
        // read
        header_ = readInstruction_ | address;
    } else {
        // write
        header_ = writeInstruction_ | address;
    }

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // start transfer of header
    start(BufferBase::Op::WRITE, &header_, 1);

    // two more steps to do (transfer data, disable CS pin)
    return 2;
    // -> DMAx_Rx_IRQHandler()
}


// SpiMaster_SPI_DMA::RegistersChannel

SpiMaster_SPI_DMA::RegistersChannel::~RegistersChannel() {
}

int SpiMaster_SPI_DMA::RegistersChannel::transferFirst(SpiMaster_SPI_DMA::BufferBase &buffer) {
    auto &r = registers();

    // set format
    r.spi.setFormat(format_);

    // start SPI (needed for newer versions to indicate that no transfer size is used)
    r.spi.start();

    // build header: instruction + address (max 7 bytes)
    uint32_t address = buffer.header<uint32_t>();
    int size = 1 + addressBytes_;
    if ((buffer.op() & BufferBase::Op::WRITE) == 0) {
        // read
        header_[0] = readInstruction_;
        size += readDummyBytes_;
    } else {
        header_[0] = writeInstruction_;
        size += writeDummyBytes_;
    }
    for (int i = addressBytes_ - 1; i >= 0; --i) {
        header_[1 + i] = address;
        address >>= 8;
    }

    // activate CS pin
    gpio::setOutput(csPin_, true);

    // start transfer of header
    start(BufferBase::Op::WRITE, header_, size);

    // two more steps to do (transfer data, disable CS pin)
    return 2;
    // -> DMAx_Rx_IRQHandler()
}

} // namespace coco
