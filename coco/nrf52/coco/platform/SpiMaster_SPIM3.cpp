#include "SpiMaster_SPIM3.hpp"
#include <coco/platform/platform.hpp>


namespace coco {

SpiMaster_SPIM3::SpiMaster_SPIM3(int sckPin, int mosiPin, int misoPin, int dcPin)
	: misoPin(misoPin), sharedPin(misoPin == dcPin)
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
	NRF_SPIM3->FREQUENCY = N(SPIM_FREQUENCY_FREQUENCY, M1);
	NRF_SPIM3->CONFIG = N(SPIM_CONFIG_CPOL, ActiveHigh)
		| N(SPIM_CONFIG_CPHA, Leading)
		| N(SPIM_CONFIG_ORDER, MsbFirst);

	// add to list of handlers
	coco::handlers.add(*this);
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

		// check for more transfers
		this->waitlist.visitSecond([this](const SpiMaster::Parameters &p) {
			startTransfer(p);
		});

		// resume first waiting coroutine
		this->waitlist.resumeFirst([](const SpiMaster::Parameters &p) {
			return true;
		});
	}
}

void SpiMaster_SPIM3::startTransfer(const SpiMaster::Parameters &p) {
	auto &channel = *reinterpret_cast<const Channel *>(p.config);

	// set CS pin
	NRF_SPIM3->PSEL.CSN = channel.csPin;

	// check if MISO and DC (data/command) are on the same pin
	if (this->sharedPin) {
		if (channel.mode != Channel::Mode::NONE) {
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
	NRF_SPIM3->DCXCNT = channel.mode == Channel::Mode::COMMAND ? 0xf : 0; // 0 for data and 0xf for command

	// set write data
	NRF_SPIM3->TXD.MAXCNT = p.writeCount;
	NRF_SPIM3->TXD.PTR = intptr_t(p.writeData);

	// set read data
	NRF_SPIM3->RXD.MAXCNT = p.readCount;
	NRF_SPIM3->RXD.PTR = intptr_t(p.readData);

	// enable and start
	NRF_SPIM3->ENABLE = N(SPIM_ENABLE_ENABLE, Enabled);
	NRF_SPIM3->TASKS_START = TRIGGER; // -> END
}


// Channel

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
		Parameters p{this, writeData, writeCount, readData, readCount};
		this->master.startTransfer(p);
	}

	return {master.waitlist, this, writeData, writeCount, readData, readCount};
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

	Parameters parameters{this, writeData, writeCount, readData, readCount};
	this->master.startTransfer(parameters);

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

} // namespace coco
