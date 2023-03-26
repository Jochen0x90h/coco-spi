#include "SpiMaster_SPIM3.hpp"
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

		// disable SPI (indicates idle state)
		NRF_SPIM3->ENABLE = 0;

		// there should be at least one pending transfer
		if (!this->transfers.empty()) {
			auto &buffer = *this->transfers.begin();
			buffer.remove2();
			buffer.completed(buffer.p.size);
		}

		// check if we need to start a new transfer
		if (!this->transfers.empty()) {
			auto &buffer = *this->transfers.begin();
			buffer.transfer();
		}
/*
		// check for more transfers
		this->waitlist.visitSecond([this](const SpiMaster::Parameters &p) {
			auto channel = reinterpret_cast<const Channel *>(p.context);
			startTransfer(p.writeData, p.writeCount, p.readData, p.readCount, channel);
		});

		// resume first waiting coroutine
		this->waitlist.resumeFirst([](const SpiMaster::Parameters &p) {
			return true;
		});*/
	}
}
/*
void SpiMaster_SPIM3::startTransfer(const void *writeData, int writeCount, void *readData, int readCount,
	const Channel *channel)
{
	// set CS pin
	NRF_SPIM3->PSEL.CSN = channel->csPin;

	// check if MISO and DC (data/command) are on the same pin
	if (this->sharedPin) {
		if (channel->mode != Channel::Mode::NONE) {
			// DC (data/command signal) overrides MISO, i.e. write-only mode
			NRF_SPIM3->PSEL.MISO = gpio::DISCONNECTED;
			NRF_SPIM3->PSELDCX = this->misoPin;
			//configureOutput(this->dcPin); // done automatically by hardware
		} else {
			// read/write: no DC signal
			NRF_SPIM3->PSELDCX = gpio::DISCONNECTED;
			NRF_SPIM3->PSEL.MISO = this->misoPin;
		}
	}

	// set command/data length
	NRF_SPIM3->DCXCNT = channel->mode == Channel::Mode::COMMAND ? 0xf : 0; // 0 for data and 0xf for command

	// set write data
	NRF_SPIM3->TXD.MAXCNT = writeCount;
	NRF_SPIM3->TXD.PTR = intptr_t(writeData);

	// set read data
	NRF_SPIM3->RXD.MAXCNT = readCount;
	NRF_SPIM3->RXD.PTR = intptr_t(readData);

	// enable and start
	NRF_SPIM3->ENABLE = N(SPIM_ENABLE_ENABLE, Enabled);
	NRF_SPIM3->TASKS_START = TRIGGER; // -> END
}*/



// Channel
/*
SpiMaster_SPIM3::Channel::Channel(SpiMaster_SPIM3 &master, int csPin, Mode mode)
	: master(master), csPin(csPin), mode(mode)
{
	// configure CS pin: output, high on idle
	gpio::setOutput(csPin, true);
	gpio::configureOutput(csPin);
}

SpiMaster_SPIM3::Channel::~Channel() {
}

Awaitable<SpiMaster::Parameters> SpiMaster_SPIM3::Channel::transfer(const void *writeData, int writeCount,
	void *readData, int readCount)
{
	// start transfer immediately if SPI is idle
	if (!NRF_SPIM3->ENABLE) {
		this->master.startTransfer(writeData, writeCount, readData, readCount, this);
	}

	return {master.waitlist, writeData, writeCount, readData, readCount, this};
}

void SpiMaster_SPIM3::Channel::transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) {
	// check if a transfer is running
	bool running = NRF_SPIM3->ENABLE;

	// wait for end of running transfer
	if (running) {
		while (!NRF_SPIM3->EVENTS_END)
			__NOP();
	}

	// clear pending interrupt flag and disable SPI
	NRF_SPIM3->EVENTS_END = 0;
	NRF_SPIM3->ENABLE = 0;

	this->master.startTransfer(writeData, writeCount, readData, readCount, this);

	// wait for end of transfer
	while (!NRF_SPIM3->EVENTS_END)
		__NOP();

	// clear pending interrupt flags at peripheral and NVIC unless a transfer was running which gets handled in handle()
	if (!running) {
		NRF_SPIM3->EVENTS_END = 0;
		clearInterrupt(SPIM3_IRQn);

		// disable SPI
		NRF_SPIM3->ENABLE = 0;
	}
}
*/

// BufferBase

SpiMaster_SPIM3::BufferBase::BufferBase(uint8_t *data, int size, SpiMaster_SPIM3 &master, int csPin, bool dcUsed)
	: coco::Buffer(data, size, BufferBase::State::READY), master(master), csPin(csPin), dcUsed(dcUsed && master.dcPin != gpio::DISCONNECTED)
{
	// configure CS pin: output, high on idle
	gpio::setOutput(csPin, true);
	gpio::configureOutput(csPin);
}

SpiMaster_SPIM3::BufferBase::~BufferBase() {
}

bool SpiMaster_SPIM3::BufferBase::start(Op op) {
	if (this->p.state != State::READY || (op & Op::READ_WRITE) == 0) {
		assert(false);
		return false;
	}

	this->op = op;
	this->master.transfers.add(*this);

	// start transfer immediately if SPI is idle
	if (!NRF_SPIM3->ENABLE)
		transfer();

	// set state
	setState(State::BUSY);

	return true;
}

void SpiMaster_SPIM3::BufferBase::cancel() {
	if (this->p.state == State::BUSY) {
		this->p.size = 0;
		setState(State::CANCELLED);
	}
}

void SpiMaster_SPIM3::BufferBase::transfer() {
	// set CS pin
	NRF_SPIM3->PSEL.CSN = this->csPin;

	// check if MISO and DC (data/command) are on the same pin
	if (this->master.sharedPin) {
		if (this->dcUsed) {
			// DC (data/command signal) overrides MISO, i.e. write-only mode
			NRF_SPIM3->PSEL.MISO = gpio::DISCONNECTED;
			NRF_SPIM3->PSELDCX = this->master.dcPin;
			//configureOutput(this->dcPin); // done automatically by hardware
		} else {
			// read/write: no DC signal
			NRF_SPIM3->PSELDCX = gpio::DISCONNECTED;
			NRF_SPIM3->PSEL.MISO = this->master.dcPin;
		}
	}

	int commandCount = std::min(int(this->op & Op::COMMAND_MASK) >> COMMAND_SHIFT, this->p.size);
	int writeCount = (this->op & Op::WRITE) != 0 || commandCount == 15 ? this->p.size : commandCount;
	int readCount = (this->op & Op::READ) != 0 ? this->p.size : 0;

	// set command/data length
	NRF_SPIM3->DCXCNT = commandCount;

	// set write data
	NRF_SPIM3->TXD.MAXCNT = writeCount;
	NRF_SPIM3->TXD.PTR = intptr_t(this->p.data);

	// set read data
	NRF_SPIM3->RXD.MAXCNT = readCount;
	NRF_SPIM3->RXD.PTR = intptr_t(this->p.data);

	// enable and start
	NRF_SPIM3->ENABLE = N(SPIM_ENABLE_ENABLE, Enabled);
	NRF_SPIM3->TASKS_START = TRIGGER; // -> END
}



} // namespace coco
