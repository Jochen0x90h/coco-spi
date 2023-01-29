#include "SpiMaster_SPI1.hpp"
#include <coco/platform/platform.hpp>
#include <algorithm>


namespace coco {

using namespace gpio;

namespace {

// for alternate functions, see Table 14 and 15 in datasheet
inline PinFunction sckFunction(int pin) {
	assert(pin == PA(5) || pin == PB(3));
	return {pin, 0};
}

inline PinFunction mosiFunction(int pin) {
	assert(pin == PA(7) || pin == PB(5));
	return {pin, 0};
}

inline PinFunction misoFunction(int pin) {
	assert(pin == PA(6) || pin == PB(4));
	return {pin, 0};
}


// CR1 register
// ------------

// SPI mode
constexpr int SPI_CR1_OFF = 0;
constexpr int SPI_CR1_FULL_DUPLEX_MASTER = SPI_CR1_MSTR;

// SPI clock phase and polarity
constexpr int SPI_CR1_CPHA_0 = 0;
constexpr int SPI_CR1_CPHA_1 = SPI_CR1_CPHA;
constexpr int SPI_CR1_CPOL_0 = 0;
constexpr int SPI_CR1_CPOL_1 = SPI_CR1_CPOL;

// SPI bit order
constexpr int SPI_CR1_LSB_FIRST = SPI_CR1_LSBFIRST;
constexpr int SPI_CR1_MSB_FIRST = 0;

// SPI prescaler
constexpr int SPI_CR1_DIV2 = 0;
constexpr int SPI_CR1_DIV4 = (SPI_CR1_BR_0);
constexpr int SPI_CR1_DIV8 = (SPI_CR1_BR_1);
constexpr int SPI_CR1_DIV16 = (SPI_CR1_BR_1 | SPI_CR1_BR_0);
constexpr int SPI_CR1_DIV32 = (SPI_CR1_BR_2);
constexpr int SPI_CR1_DIV64 = (SPI_CR1_BR_2 | SPI_CR1_BR_0);
constexpr int SPI_CR1_DIV128 = (SPI_CR1_BR_2 | SPI_CR1_BR_1);
constexpr int SPI_CR1_DIV256 = (SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0);

// CR2 register
// ------------

// SPI data size (8, 16, 32)
constexpr int SPI_CR2_DATA_SIZE(int s) { return (s - 1) << SPI_CR2_DS_Pos; }


// configuration

constexpr int SPI_CR1 = SPI_CR1_SPE
	| SPI_CR1_FULL_DUPLEX_MASTER
	| SPI_CR1_CPHA_1 | SPI_CR1_CPOL_0 // shift on rising edge, sample on falling edge
	| SPI_CR1_MSB_FIRST;
	//| SPI_CR1_DIV8;

constexpr int SPI_CR2 = SPI_CR2_DATA_SIZE(8)
	| SPI_CR2_FRXTH
	| SPI_CR2_SSOE; // single master mode

}


SpiMaster_SPI1::SpiMaster_SPI1(Loop_TIM2 &loop, Prescaler prescaler, int sckPin, int mosiPin, int misoPin)
	: cr1(SPI_CR1 | (int(prescaler) << SPI_CR1_BR_Pos))
{
	// configure SPI pins (driven low when SPI is disabled)
	configureAlternateOutput(sckFunction(sckPin));
	configureAlternateOutput(mosiFunction(mosiPin));
	configureAlternateOutput(misoFunction(misoPin));

	// initialize DMA
	RCC->AHBENR = RCC->AHBENR | RCC_AHBENR_DMAEN;
	DMA1_Channel2->CPAR = (uint32_t)&SPI1->DR;
	DMA1_Channel3->CPAR = (uint32_t)&SPI1->DR;
	RCC->AHBENR = RCC->AHBENR & ~RCC_AHBENR_DMAEN;

	// add to list of handlers
	loop.handlers.add(*this);
}

SpiMaster_SPI1::~SpiMaster_SPI1() {
}

void SpiMaster_SPI1::handle() {
	// check if read DMA has completed
	if ((DMA1->ISR & DMA_ISR_TCIF2) != 0) {
		// clear interrupt flags at DMA and NVIC
		DMA1->IFCR = DMA_IFCR_CTCIF2;
		clearInterrupt(DMA1_Ch2_3_DMA2_Ch1_2_IRQn);

		if (update()) {
			// end of transfer

			// disable clocks
			RCC->APB2ENR = RCC->APB2ENR & ~RCC_APB2ENR_SPI1EN;
			RCC->AHBENR = RCC->AHBENR & ~RCC_AHBENR_DMAEN;

			// check for more transfers
			this->waitlist.visitSecond([this](const SpiMaster::Parameters &p) {
				auto channel = reinterpret_cast<const Channel *>(p.context);
				startTransfer(p.writeData, p.writeCount, p.readData, p.readCount, channel);
			});

			// resume first waiting coroutine
			this->waitlist.resumeFirst([](const SpiMaster::Parameters &p) {
				return true;
			});
		}
	}
}

void SpiMaster_SPI1::startTransfer(const void *writeData, int writeCount, void *readData, int readCount,
	const Channel *channel)
{
	// enable clocks
	RCC->APB2ENR = RCC->APB2ENR | RCC_APB2ENR_SPI1EN;
	RCC->AHBENR = RCC->AHBENR | RCC_AHBENR_DMAEN;

	// set transfer state
	this->csPin = channel->csPin;
	this->readCount = readCount;
	this->readAddress = (uint32_t)readData;
	this->writeCount = writeCount;
	this->writeAddress = (uint32_t)writeData;

	// update transfer
	update();

	// now wait for DMA_ISR_TCIF2 flag
}

bool SpiMaster_SPI1::update() {
	if (this->readCount > 0) {
		SPI1->CR1 = 0;
		DMA1_Channel2->CCR = 0;
		DMA1_Channel3->CCR = 0;
		SPI1->CR2 = SPI_CR2 | SPI_CR2_RXDMAEN; // first enable receive DMA according to data sheet
		if (this->writeCount > 0) {
			// read and write
			int count = std::min(this->readCount, this->writeCount);
			DMA1_Channel2->CNDTR = count;
			DMA1_Channel2->CMAR = this->readAddress;
			DMA1_Channel2->CCR = DMA_CCR_EN // enable read channel
				| DMA_CCR_TCIE // transfer complete interrupt enable
				| DMA_CCR_MINC; // auto-increment memory
			DMA1_Channel3->CNDTR = count;
			DMA1_Channel3->CMAR = this->writeAddress;
			DMA1_Channel3->CCR = DMA_CCR_EN // enable write channel
				| DMA_CCR_DIR // read from memory
				| DMA_CCR_MINC; // auto-increment memory

			this->readAddress += count;
			this->writeAddress += count;
			this->readCount -= count;
			this->writeCount -= count;
		} else {
			// read only
			DMA1_Channel2->CNDTR = this->readCount;
			DMA1_Channel2->CMAR = this->readAddress;
			DMA1_Channel2->CCR = DMA_CCR_EN // enable read channel
				| DMA_CCR_TCIE // transfer complete interrupt enable
				| DMA_CCR_MINC; // auto-increment memory
			DMA1_Channel3->CNDTR = this->readCount;
			DMA1_Channel3->CMAR = (uint32_t)&this->writeDummy; // write from dummy
			DMA1_Channel3->CCR = DMA_CCR_EN // enable write channel
				| DMA_CCR_DIR; // read from memory

			this->readCount = 0;
		}
	} else if (this->writeCount > 0) {
		// write only
		SPI1->CR1 = 0;
		DMA1_Channel2->CCR = 0;
		DMA1_Channel3->CCR = 0;
		SPI1->CR2 = SPI_CR2 | SPI_CR2_RXDMAEN; // first enable receive DMA according to data sheet

		DMA1_Channel2->CNDTR = this->writeCount;
		DMA1_Channel2->CMAR = (uint32_t)&this->readDummy; // read into dummy
		DMA1_Channel2->CCR = DMA_CCR_EN // enable read channel
			| DMA_CCR_TCIE; // transfer complete interrupt enable
		DMA1_Channel3->CNDTR = this->writeCount;
		DMA1_Channel3->CMAR = this->writeAddress;
		DMA1_Channel3->CCR = DMA_CCR_EN // enable write channel
			| DMA_CCR_DIR // read from memory
			| DMA_CCR_MINC; // auto-increment memory

		this->writeCount = 0;
	} else {
		// end of transfer

		// set CS pin high
		gpio::setOutput(this->csPin, true);

		// disable SPI and DMA
		SPI1->CR1 = 0;
		DMA1_Channel2->CCR = 0;
		DMA1_Channel3->CCR = 0;

		return true;
	}

	// set CS pin low
	gpio::setOutput(this->csPin, false);

	// start SPI
	SPI1->CR2 = SPI_CR2 | SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
	SPI1->CR1 = this->cr1;

	return false;
}


// Channel

SpiMaster_SPI1::Channel::Channel(SpiMaster_SPI1 &master, int csPin)
	: master(master), csPin(csPin)
{
	// configure CS pin: output, high on idle
	gpio::setOutput(csPin, true);
	gpio::configureOutput(csPin);
}

SpiMaster_SPI1::Channel::~Channel() {
}

Awaitable<SpiMaster::Parameters> SpiMaster_SPI1::Channel::transfer(const void *writeData, int writeCount,
	void *readData, int readCount)
{
	// start transfer immediately if SPI is idle
	if (DMA1_Channel2->CCR == 0) {
		this->master.startTransfer(writeData, writeCount, readData, readCount, this);
	}

	return {master.waitlist, writeData, writeCount, readData, readCount, this};
}

void SpiMaster_SPI1::Channel::transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) {
	// check if a transfer is running
	bool running = DMA1_Channel2->CCR != 0;

	// wait for end of running transfer
	if (running) {
		do {
			while ((DMA1->ISR & DMA_ISR_TCIF2) == 0)
				__NOP();
		} while (!this->master.update());
	}

	// clear pending interrupt flag and disable SPI and DMA
	DMA1->IFCR = DMA_IFCR_CTCIF2;

	this->master.startTransfer(writeData, writeCount, readData, readCount, this);

	// wait for end of transfer
	do {
		while ((DMA1->ISR & DMA_ISR_TCIF2) == 0)
			__NOP();
	} while (!this->master.update());

	// clear pending interrupt flags at DMA and NVIC unless a transfer was running which gets handled in handle()
	if (!running) {
		DMA1->IFCR = DMA_IFCR_CTCIF2;
		clearInterrupt(DMA1_Ch2_3_DMA2_Ch1_2_IRQn);

		// disable clocks
		RCC->APB2ENR = RCC->APB2ENR & ~RCC_APB2ENR_SPI1EN;
		RCC->AHBENR = RCC->AHBENR & ~RCC_AHBENR_DMAEN;
	}
}

} // namespace coco
