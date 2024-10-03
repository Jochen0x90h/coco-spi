#include "SpiMaster_SPI_DMA.hpp"
//#include <coco/debug.hpp>


namespace coco {

// SpiMaster_SPI_DMA

SpiMaster_SPI_DMA::SpiMaster_SPI_DMA(Loop_Queue &loop,
    gpio::Config sckPin, gpio::Config misoPin, gpio::Config mosiPin, gpio::Config dcPin,
    const spi::Info &spiInfo, const dma::Info2 &dmaInfo, spi::Config config)
    : loop(loop)
    , dcPin(dcPin)
    , sharedPin(dcPin != gpio::Config::NONE && gpio::getPinIndex(dcPin) == gpio::getPinIndex(misoPin))
{
    // enable clocks (note two cycles wait time until peripherals can be accessed, see STM32G4 reference manual section 7.2.17)
    spiInfo.rcc.enableClock();
    dmaInfo.rcc.enableClock();

    // configure SCK pin
    gpio::configureAlternate(sckPin);

    // configure MISO and DC pins, may be shared
    if (misoPin != gpio::Config::NONE)
        gpio::configureAlternate(misoPin);
    if (dcPin != gpio::Config::NONE)
        gpio::configureOutput(dcPin, false); // does not change alternate function register

    // configure MOSI pin
    if (mosiPin != gpio::Config::NONE)
        gpio::configureAlternate(mosiPin);

    // configure SPI
    auto spi = this->spi = spiInfo.spi;
    spi->CR2 = spi::CR2(config) // user provided configuration
        | SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN // enable DMA
        | SPI_CR2_SSOE; // single master mode (todo: find out why this flag is needed)

    // configure RX DMA channel
    this->rxStatus = dmaInfo.status1();
    this->rxChannel = dmaInfo.channel1();
    this->rxChannel.setPeripheralAddress(&spi->DR);
    this->rxDmaIrq = dmaInfo.irq1;
    nvic::setPriority(this->rxDmaIrq, nvic::Priority::MEDIUM); // interrupt gets enabled in first call to start()

    // configure TX DMA channel
    this->txStatus = dmaInfo.status2();
    this->txChannel = dmaInfo.channel2();
    this->txChannel.setPeripheralAddress(&spi->DR);

    // map DMA to SPI
    spiInfo.map(dmaInfo);

    // permanently enable SPI to ensure the right idle level for the clock
    // (does not start until TXFIFO gets written or TX DMA enabled)
    spi->CR1 = spi::CR1(config) // user provided configuration
        | SPI_CR1_MSTR // master mode
        | SPI_CR1_SPE; // enable
}

void SpiMaster_SPI_DMA::DMA_Rx_IRQHandler() {
    // check if read DMA has completed
    if ((this->rxStatus.get() & dma::Status::Flags::TRANSFER_COMPLETE) != 0) {
        // clear interrupt flag
        this->rxStatus.clear(dma::Status::Flags::TRANSFER_COMPLETE);

        // also clear tx flag, needed on STM32F4
        this->txStatus.clear(dma::Status::Flags::TRANSFER_COMPLETE);

        // disable DMA
        this->rxChannel.disable();
        this->txChannel.disable();

        // check for second transfer
        auto op = this->transfer2;
        if (op != BufferBase::Op::NONE) {
            this->transfer2 = BufferBase::Op::NONE;

            auto &buffer = this->transfers.front();

            // set DC pin high to indicate data or keep low when everything is a command
            if (buffer.channel.dcUsed && (buffer.op & coco::Buffer::Op::COMMAND) == 0)
                gpio::setOutput(this->dcPin, true);

            int headerSize = buffer.p.headerSize;
            auto data = buffer.p.data + headerSize;
            int size = buffer.p.size - headerSize;

            this->rxChannel.setCount(size);
            this->txChannel.setCount(size);
            this->txChannel.setMemoryAddress(data);
            if (op == coco::Buffer::Op::WRITE) {
                // start DMA for write only
                this->rxChannel.setMemoryAddress(&this->dummy); // read into dummy
                this->rxChannel.enable(dma::Channel::Config::PERIPHERAL_TO_MEMORY | dma::Channel::Config::TRANSFER_COMPLETE_INTERRUPT);
            } else {
                // start DMA for write and read
                this->rxChannel.setMemoryAddress(data);
                this->rxChannel.enable(dma::Channel::Config::RX | dma::Channel::Config::TRANSFER_COMPLETE_INTERRUPT);
            }
            this->txChannel.enable(dma::Channel::Config::TX);

            // -> DMAx_Rx_IRQHandler()
        } else {
            // end of transfer
            this->transfers.pop(
                [this](BufferBase &buffer) {
                    // deactivate CS pin
                    //if ((buffer.op & BufferBase::Op::PARTIAL) == 0)
                        gpio::setOutput(buffer.channel.csPin, false);

                    // notify app that buffer has finished
                    this->loop.push(buffer);
                    return true;
                },
                [](BufferBase &next) {
                    // start next buffer
                    next.start();
                }
            );
        }
    }
}


// BufferBase

SpiMaster_SPI_DMA::BufferBase::BufferBase(uint8_t *data, int capacity, Channel &channel)
    : coco::Buffer(data, capacity, BufferBase::State::READY), channel(channel)
{
    channel.buffers.add(*this);
}

SpiMaster_SPI_DMA::BufferBase::~BufferBase() {
}

bool SpiMaster_SPI_DMA::BufferBase::start(Op op) {
    if (this->st.state != State::READY) {
        assert(this->st.state != State::BUSY);
        return false;
    }

    // check if READ or WRITE flag is set
    assert((op & Op::READ_WRITE) != 0);

    this->op = op;
    auto &device = this->channel.device;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers.push(nvic::Guard(device.rxDmaIrq), *this))
        start();

    // set state
    setBusy();

    return true;
}

bool SpiMaster_SPI_DMA::BufferBase::cancel() {
    if (this->st.state != State::BUSY)
        return false;
    auto &device = this->channel.device;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers.remove(nvic::Guard(device.rxDmaIrq), *this, false)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt should be enabled at this point
        setReady(0);
    }

    return true;
}

void SpiMaster_SPI_DMA::BufferBase::start() {
    auto &device = this->channel.device;

    int headerSize = this->p.headerSize;
    auto op = this->op & Op::READ_WRITE;
    bool allCommand = (this->op & Op::COMMAND) != 0;

    // check if MISO and DC (data/command) share the the same pin
    if (device.sharedPin)
        gpio::setMode(device.dcPin, this->channel.dcUsed ? gpio::Mode::OUTPUT : gpio::Mode::ALTERNATE);

    // set D/nC pin (low: command, high: data)
    if (this->channel.dcUsed)
        gpio::setOutput(device.dcPin, !(headerSize > 0 || allCommand));

    // activate CS pin
    gpio::setOutput(this->channel.csPin, true);

    auto data = this->p.data;
    device.txChannel.setMemoryAddress(data);
    if (headerSize > 0 && !allCommand && this->channel.dcUsed) {
        // two transfers for header and data using dc pin
        device.transfer2 = op;

        // start DMA for write only
        device.rxChannel.setCount(headerSize);
        device.txChannel.setCount(headerSize);
        device.rxChannel.setMemoryAddress(&device.dummy); // read into dummy
        device.rxChannel.enable(dma::Channel::Config::PERIPHERAL_TO_MEMORY | dma::Channel::Config::TRANSFER_COMPLETE_INTERRUPT);
    } else {
        // one transfer
        int count = this->p.size;
        device.rxChannel.setCount(count);
        device.txChannel.setCount(count);
        if (op == Op::WRITE) {
            // start DMA for write only
            device.rxChannel.setMemoryAddress(&device.dummy); // read into dummy
            device.rxChannel.enable(dma::Channel::Config::PERIPHERAL_TO_MEMORY | dma::Channel::Config::TRANSFER_COMPLETE_INTERRUPT);
        } else {
            // start DMA for write and read
            device.rxChannel.setMemoryAddress(data); // read into dummy
            device.rxChannel.enable(dma::Channel::Config::RX | dma::Channel::Config::TRANSFER_COMPLETE_INTERRUPT);
        }
    }
    device.txChannel.enable(dma::Channel::Config::TX);
}

void SpiMaster_SPI_DMA::BufferBase::handle() {
    setReady();
}


// Channel

SpiMaster_SPI_DMA::Channel::Channel(SpiMaster_SPI_DMA &device, gpio::Config csPin, bool dcUsed)
    : BufferDevice(State::READY)
    , device(device), csPin(csPin), dcUsed(dcUsed)
{
    // configure CS pin
    gpio::configureOutput(csPin, false);
}

SpiMaster_SPI_DMA::Channel::~Channel() {
}

int SpiMaster_SPI_DMA::Channel::getBufferCount() {
    return this->buffers.count();
}

SpiMaster_SPI_DMA::BufferBase &SpiMaster_SPI_DMA::Channel::getBuffer(int index) {
    return this->buffers.get(index);
}

} // namespace coco
