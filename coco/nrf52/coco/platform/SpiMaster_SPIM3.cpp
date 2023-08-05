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
		// clear pending interrupt flags at peripheral and NVIC
		NRF_SPIM3->EVENTS_END = 0;
		clearInterrupt(SPIM3_IRQn);

		// disable SPI
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
	std::copy(data, data + size, this->dat - size);
	this->headerSize = size;

	// todo: check max size
	return true;
}

bool SpiMaster_SPIM3::BufferBase::startInternal(int size, Op op) {
	if (this->stat != State::READY) {
		assert(this->stat != State::BUSY);
		return false;
	}

	// check if READ or WRITE flag is set
	assert((op & Op::READ_WRITE) != 0);

	auto &device = this->channel.device;
	this->xferred = size;
	this->op = op;

	// start transfer immediately if SPI is idle
	if (device.transfers.empty())
		transfer();

	// add to list of pending transfers
	device.transfers.add(*this);

	// set state
	setBusy();

	return true;
}

void SpiMaster_SPIM3::BufferBase::cancel() {
	if (this->stat != State::BUSY)
		return;

	auto &device = this->channel.device;
	auto current = device.transfers.begin();

	// can be cancelled immediately if not yet in progress, otherwise cancel has no effect
	if (this != &*current) {
		remove2();
		setReady(0);
	}
}

void SpiMaster_SPIM3::BufferBase::transfer() {
	auto &device = this->channel.device;

	// set CS pin
	NRF_SPIM3->PSEL.CSN = this->channel.csPin;

	// check if MISO and DC (data/command) are on the same pin
	if (device.sharedPin) {
		if (this->channel.dcUsed) {
			// DC (data/command signal) overrides MISO, i.e. write-only mode
			NRF_SPIM3->PSEL.MISO = gpio::DISCONNECTED;
			NRF_SPIM3->PSELDCX = device.dcPin;
			//configureOutput(this->dcPin); // done automatically by hardware
		} else {
			// read/write: no DC signal
			NRF_SPIM3->PSELDCX = gpio::DISCONNECTED;
			NRF_SPIM3->PSEL.MISO = device.dcPin;
		}
	}

	int headerSize = this->headerSize;
	int size = this->xferred;

	int commandCount = (this->op & Op::COMMAND) != 0 ? 15 : headerSize;
	int writeCount = headerSize + ((this->op & Op::WRITE) != 0 ? size : 0);
	int readCount = (this->op & Op::READ) != 0 ? (headerSize + size) : 0;
	auto data = uintptr_t(this->dat - headerSize);

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
}


// Channel

SpiMaster_SPIM3::Channel::Channel(SpiMaster_SPIM3 &device, int csPin, bool dcUsed)
	: device(device), csPin(csPin), dcUsed(dcUsed && device.dcPin != gpio::DISCONNECTED)
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
	return {this->device.stateTasks, state};
}

int SpiMaster_SPIM3::Channel::getBufferCount() {
	return this->buffers.count();
}

SpiMaster_SPIM3::BufferBase &SpiMaster_SPIM3::Channel::getBuffer(int index) {
	return this->buffers.get(index);
}

} // namespace coco
