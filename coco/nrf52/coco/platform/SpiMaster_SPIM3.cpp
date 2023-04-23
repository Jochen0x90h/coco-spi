#include "SpiMaster_SPIM3.hpp"
#include <coco/debug.hpp>
#include <coco/platform/platform.hpp>


namespace coco {

SpiMaster_SPIM3::SpiMaster_SPIM3(Loop_RTC0 &loop, Speed speed, int sckPin, int mosiPin, int misoPin, int dcPin)
	: dcPin(dcPin), sharedPin(misoPin == dcPin)
{
	// configure SCK pin: output, low on idle
	gpio::configureOutput(sckPin);
	NRF_SPIM3->PSEL.SCK = sckPin;

	// configure MOSI pin: output, high on idle
	gpio::setOutput(mosiPin, true);
	gpio::configureOutput(mosiPin);
	NRF_SPIM3->PSEL.MOSI = mosiPin;

	// configure MISO pin: input, pull up
	gpio::configureInput(misoPin, gpio::Pull::UP);
	if (!this->sharedPin) {
		NRF_SPIM3->PSEL.MISO = misoPin;

		// configure DC pin if used: output, low on idle
		if (dcPin != gpio::DISCONNECTED) {
			gpio::configureOutput(dcPin);
			NRF_SPIM3->PSELDCX = dcPin;
		}
	} else {
		// MISO and DC share the same pin, configure in startTransfer()
	}

	// configure SPI
	NRF_SPIM3->INTENSET = N(SPIM_INTENSET_END, Set);
	NRF_SPIM3->FREQUENCY = int(speed);
	NRF_SPIM3->CONFIG = N(SPIM_CONFIG_CPOL, ActiveHigh)
		| N(SPIM_CONFIG_CPHA, Leading)
		| N(SPIM_CONFIG_ORDER, MsbFirst);

	// add to list of handlers
	loop.handlers.add(*this);
}

SpiMaster_SPIM3::~SpiMaster_SPIM3() {
}

void SpiMaster_SPIM3::handle() {
	if (NRF_SPIM3->EVENTS_END) {
//debug::setRed();
		// clear pending interrupt flags at peripheral and NVIC
		NRF_SPIM3->EVENTS_END = 0;
		clearInterrupt(SPIM3_IRQn);

		// disable SPI (indicates idle state)
		NRF_SPIM3->ENABLE = 0;

		auto current = this->transfers.begin();
		if (current != this->transfers.end()) {
			// check if more transfers are pending
			auto next = current;
			++next;
			if (next != this->transfers.end())
				next->transfer();

			// set current buffer to ready state and resume waiting coroutines
			current->remove2();
			current->setReady();
		}
	}
}


// BufferBase

SpiMaster_SPIM3::BufferBase::BufferBase(uint8_t *data, int capacity, Channel &channel)
	: BufferImpl(data, capacity, BufferBase::State::READY), channel(channel)
{
	channel.buffers.add(*this);
}

SpiMaster_SPIM3::BufferBase::~BufferBase() {
}

bool SpiMaster_SPIM3::BufferBase::setHeader(const uint8_t *data, int size) {
	// copy header before start of buffer data
	std::copy(data, data + size, this->buffer.data - size);
	this->headerSize = size;

	// todo: check max size
	return true;
}

bool SpiMaster_SPIM3::BufferBase::startInternal(int size, Op op) {
	if (this->p.state != State::READY) {
		assert(false);
		return false;
	}

	// check if READ or WRITE flag is set
	assert((op & Op::READ_WRITE) != 0);

	auto &master = this->channel.master;
	this->p.result.transferred = size;
	this->op = op;

	// add to list of pending transfers
	this->inProgress = false;
	master.transfers.add(*this);

	// start transfer immediately if SPI is idle
	if (!NRF_SPIM3->ENABLE)
		transfer();

	// set state
	setBusy();

	return true;
}

void SpiMaster_SPIM3::BufferBase::cancel() {
	if (this->p.state != State::BUSY)
		return;

	// can be cancelled immediately if not yet in progress, otherwise cancel has no effect
	if (!this->inProgress) {
		remove2();
		setReady(0);
	}
}

void SpiMaster_SPIM3::BufferBase::transfer() {
	this->inProgress = true;
	auto &master = this->channel.master;

	// set CS pin
	NRF_SPIM3->PSEL.CSN = this->channel.csPin;

	// check if MISO and DC (data/command) are on the same pin
	if (master.sharedPin) {
		if (this->channel.dcUsed) {
			// DC (data/command signal) overrides MISO, i.e. write-only mode
			NRF_SPIM3->PSEL.MISO = gpio::DISCONNECTED;
			NRF_SPIM3->PSELDCX = master.dcPin;
			//configureOutput(this->dcPin); // done automatically by hardware
		} else {
			// read/write: no DC signal
			NRF_SPIM3->PSELDCX = gpio::DISCONNECTED;
			NRF_SPIM3->PSEL.MISO = master.dcPin;
		}
	}

	int headerSize = this->headerSize;
	int size = this->p.result.transferred;

	int commandCount = (this->op & Op::COMMAND) != 0 ? 15 : headerSize;
	int writeCount = headerSize + ((this->op & Op::WRITE) != 0 ? size : 0);
	int readCount = (this->op & Op::READ) != 0 ? (headerSize + size) : 0;
	auto data = intptr_t(this->buffer.data - headerSize);

	// set command/data length
	NRF_SPIM3->DCXCNT = commandCount;

	// set write data
	NRF_SPIM3->TXD.MAXCNT = writeCount;
	NRF_SPIM3->TXD.PTR = data;

	// set read data
	NRF_SPIM3->RXD.MAXCNT = readCount;
	NRF_SPIM3->RXD.PTR = data;

	// enable and start
	NRF_SPIM3->ENABLE = N(SPIM_ENABLE_ENABLE, Enabled);
	NRF_SPIM3->TASKS_START = TRIGGER; // -> END

	//debug::setGreen();
}


// Channel

SpiMaster_SPIM3::Channel::Channel(SpiMaster_SPIM3 &master, int csPin, bool dcUsed)
	: master(master), csPin(csPin), dcUsed(dcUsed && master.dcPin != gpio::DISCONNECTED)
{
	// configure CS pin: output, high on idle
	gpio::setOutput(csPin, true);
	gpio::configureOutput(csPin);
}

SpiMaster_SPIM3::Channel::~Channel() {
}

Device::State SpiMaster_SPIM3::Channel::state() {
	return State::READY;
}

Awaitable<Device::State> SpiMaster_SPIM3::Channel::untilState(State state) {
	if (state == State::READY)
		return {};
	return {this->master.stateTasks, state};
}

int SpiMaster_SPIM3::Channel::getBufferCount() {
	return this->buffers.count();
}

SpiMaster_SPIM3::BufferBase &SpiMaster_SPIM3::Channel::getBuffer(int index) {
	return this->buffers.get(index);
}

} // namespace coco
