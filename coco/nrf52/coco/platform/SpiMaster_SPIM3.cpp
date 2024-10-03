#include "SpiMaster_SPIM3.hpp"
#include <coco/debug.hpp>
#include <coco/platform/nvic.hpp>


namespace coco {

SpiMaster_SPIM3::SpiMaster_SPIM3(Loop_Queue &loop,
    gpio::Config sckPin, gpio::Config misoPin, gpio::Config mosiPin, gpio::Config dcPin,
    spi::Config config)
    : loop(loop)
    , dcPin(dcPin)
    , sharedPin(dcPin != gpio::Config::NONE && gpio::getPinIndex(dcPin) == gpio::getPinIndex(misoPin))
{
    // configure SCK pin
    gpio::configureAlternate(sckPin);
    NRF_SPIM3->PSEL.SCK = gpio::getPinIndex(sckPin);

    // configure MISO pin
    if (misoPin != gpio::Config::NONE) {
        gpio::configureAlternate(misoPin);
        NRF_SPIM3->PSEL.MISO = gpio::getPinIndex(misoPin);
    }

    // configure MOSI pin
    if (mosiPin != gpio::Config::NONE) {
        gpio::configureAlternate(mosiPin);
        NRF_SPIM3->PSEL.MOSI = gpio::getPinIndex(mosiPin);
    }

    // configure DC pin
    if (dcPin != gpio::Config::NONE) {
        gpio::configureAlternate(dcPin);

        // if MISO and DC share the same pin, configure in startTransfer()
        if (!this->sharedPin)
            NRF_SPIM3->PSELDCX = gpio::getPinIndex(dcPin);
    }

    // configure SPI
    NRF_SPIM3->INTENSET = N(SPIM_INTENSET_END, Set);
    NRF_SPIM3->FREQUENCY = int(config & spi::Config::SPEED_MASK);
    NRF_SPIM3->CONFIG = int(config & spi::Config::CONFIG_MASK);

    // permanently enable SPI to ensure the right idle level for the clock
    NRF_SPIM3->ENABLE = N(SPIM_ENABLE_ENABLE, Enabled);
}

void SpiMaster_SPIM3::SPIM3_IRQHandler() {
    if (NRF_SPIM3->EVENTS_END) {
        // clear pending interrupt flags at peripheral and NVIC
        NRF_SPIM3->EVENTS_END = 0;

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


// BufferBase

SpiMaster_SPIM3::BufferBase::BufferBase(uint8_t *data, int capacity, Channel &channel)
    : coco::Buffer(data, capacity, BufferBase::State::READY), channel(channel)
{
    channel.buffers.add(*this);
}

SpiMaster_SPIM3::BufferBase::~BufferBase() {
}

bool SpiMaster_SPIM3::BufferBase::start(Op op) {
    if (this->st.state != State::READY) {
        assert(this->st.state != State::BUSY);
        return false;
    }

    // check if READ or WRITE flag is set
    assert((op & Op::READ_WRITE) != 0);

    this->op = op;
    auto &device = this->channel.device;

    // add to list of pending transfers and start immediately if list was empty
    if (device.transfers.push(nvic::Guard(SPIM3_IRQn), *this))
        start();

    // set state
    setBusy();

    return true;
}

bool SpiMaster_SPIM3::BufferBase::cancel() {
    if (this->st.state != State::BUSY)
        return false;
    auto &device = this->channel.device;

    // remove from pending transfers if not yet started, otherwise complete normally
    if (device.transfers.remove(nvic::Guard(SPIM3_IRQn), *this, false)) {
        // cancel succeeded: set buffer ready again
        // resume application code, therefore interrupt should be enabled at this point
        setReady(0);
    }
    return true;
}

void SpiMaster_SPIM3::BufferBase::start() {
    auto &device = this->channel.device;

    // activate CS pin
    gpio::setOutput(this->channel.csPin, true);

    // check if MISO and DC (data/command) are on the same pin
    if (device.sharedPin) {
        if (this->channel.dcUsed) {
            // DC (data/command signal) overrides MISO, i.e. write-only mode
            NRF_SPIM3->PSEL.MISO = gpio::DISCONNECTED;
            NRF_SPIM3->PSELDCX = gpio::getPinIndex(device.dcPin);
            //configureOutput(this->dcPin); // done automatically by hardware
        } else {
            // read/write: no DC signal
            NRF_SPIM3->PSELDCX = gpio::DISCONNECTED;
            NRF_SPIM3->PSEL.MISO = gpio::getPinIndex(device.dcPin);
        }
    }

    int headerSize = this->p.headerSize;
    int size = this->p.size;

    int commandCount = (this->op & Op::COMMAND) != 0 ? 15 : headerSize;
    int writeCount = (this->op & Op::WRITE) != 0 ? size : headerSize;
    int readCount = (this->op & Op::READ) != 0 ? size : 0;
    auto data = uintptr_t(this->p.data);

    // set command/data length
    NRF_SPIM3->DCXCNT = commandCount;

    // set write data
    NRF_SPIM3->TXD.MAXCNT = writeCount;
    NRF_SPIM3->TXD.PTR = data;

    // set read data
    NRF_SPIM3->RXD.MAXCNT = readCount;
    NRF_SPIM3->RXD.PTR = data;

    // start
    NRF_SPIM3->TASKS_START = TRIGGER; // -> SPIM3_IRQHandler()
}

void SpiMaster_SPIM3::BufferBase::handle() {
    setReady();
}


// Channel

SpiMaster_SPIM3::Channel::Channel(SpiMaster_SPIM3 &device, gpio::Config csPin, bool dcUsed)
    : BufferDevice(State::READY)
    , device(device), csPin(csPin), dcUsed(dcUsed)
{
    // configure CS pin
    gpio::configureOutput(csPin, false);
}

SpiMaster_SPIM3::Channel::~Channel() {
}

int SpiMaster_SPIM3::Channel::getBufferCount() {
    return this->buffers.count();
}

SpiMaster_SPIM3::BufferBase &SpiMaster_SPIM3::Channel::getBuffer(int index) {
    return this->buffers.get(index);
}

} // namespace coco
