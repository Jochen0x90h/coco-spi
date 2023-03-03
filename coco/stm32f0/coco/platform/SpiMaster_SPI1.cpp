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


// dma

static void enableDma() {
	// first enable receive DMA at SPI according to data sheet
	SPI1->CR2 = SPI_CR2 | SPI_CR2_RXDMAEN;

	// enable read channel
	DMA1_Channel2->CCR = DMA_CCR_EN
		| DMA_CCR_TCIE // transfer complete interrupt enable
		| DMA_CCR_MINC; // auto-increment memory
	
	// enable write channel
	DMA1_Channel3->CCR = DMA_CCR_EN 
		| DMA_CCR_DIR // read from memory
		| DMA_CCR_MINC; // auto-increment memory

	// now also enable send DMA at SPI
	SPI1->CR2 = SPI_CR2 | SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
}

static void enableDmaRead() {
	// first enable receive DMA at SPI according to data sheet
	SPI1->CR2 = SPI_CR2 | SPI_CR2_RXDMAEN;

	// enable read channel
	DMA1_Channel2->CCR = DMA_CCR_EN
		| DMA_CCR_TCIE // transfer complete interrupt enable
		| DMA_CCR_MINC; // auto-increment memory
	
	// enable write channel (writes zero byte)
	DMA1_Channel3->CCR = DMA_CCR_EN 
		| DMA_CCR_DIR; // read from memory

	// now also enable send DMA at SPI
	SPI1->CR2 = SPI_CR2 | SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
}

static void enableDmaWrite() {
	// first enable receive DMA at SPI according to data sheet
	SPI1->CR2 = SPI_CR2 | SPI_CR2_RXDMAEN;

	// enable read channel (reads into dummy byte)
	DMA1_Channel2->CCR = DMA_CCR_EN
		| DMA_CCR_TCIE; // transfer complete interrupt enable

	// enable write channel
	DMA1_Channel3->CCR = DMA_CCR_EN 
		| DMA_CCR_DIR // read from memory
		| DMA_CCR_MINC; // auto-increment memory

	// now also enable send DMA at SPI
	SPI1->CR2 = SPI_CR2 | SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
}


// SpiMaster_SPI1

SpiMaster_SPI1::SpiMaster_SPI1(Loop_TIM2 &loop, Prescaler prescaler, int sckPin, int mosiPin, int misoPin, int dcPin)
	: cr1(SPI_CR1 | (int(prescaler) << SPI_CR1_BR_Pos)), dcPin(dcPin), sharedPin(misoPin == dcPin)
{
	// configure SPI pins (driven low when SPI is disabled)
	configureAlternateOutput(sckFunction(sckPin));
	configureAlternateOutput(mosiFunction(mosiPin));
	configureAlternateOutput(misoFunction(misoPin));

	// configure DC pin if used: output
	if (dcPin >= 0 && dcPin != misoPin)
		gpio::configureOutput(dcPin);

	// enable clock and initialize DMA
	RCC->AHBENR = RCC->AHBENR | RCC_AHBENR_DMAEN;
	DMA1_Channel2->CPAR = (uint32_t)&SPI1->DR; // data register
	DMA1_Channel3->CPAR = (uint32_t)&SPI1->DR;

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

		int transfer2 = this->transfer2;
		if (transfer2 != 0) {
			this->transfer2 = 0;

			// set DC pin high to indicate data
			gpio::setOutput(this->dcPin, true); 

			// disable SPI and DMA
			SPI1->CR1 = 0;
			DMA1_Channel2->CCR = 0;
			DMA1_Channel3->CCR = 0;

			auto data = this->data;
			int count = this->count;
			DMA1_Channel2->CNDTR = count;
			DMA1_Channel3->CNDTR = count;
			if ((transfer2 & 3) == 3) {
				// write and read
				DMA1_Channel2->CMAR = data;
				DMA1_Channel3->CMAR = data;
				enableDma();  
			} else if ((transfer2 & 1) != 0) {
				// write only
				DMA1_Channel2->CMAR = (intptr_t)&this->dummy; // read into dummy
				DMA1_Channel3->CMAR = data;
				enableDmaWrite();  
			} else {
				// read only
				DMA1_Channel2->CMAR = data;
				DMA1_Channel3->CMAR = (intptr_t)&this->zero; // write zero
				enableDmaRead(); 
			}

			// start SPI for second transfer
			SPI1->CR1 = SPI_CR1; // -> DMA_ISR_TCIF2  
		} else {
			// end of transfer

			// set CS pin high
			gpio::setOutput(this->csPin, true);

			// disable SPI and DMA
			SPI1->CR1 = 0;
			DMA1_Channel2->CCR = 0;
			DMA1_Channel3->CCR = 0;

			// disable SPI clock
			RCC->APB2ENR = RCC->APB2ENR & ~RCC_APB2ENR_SPI1EN;

			// there should be at least one pending transfer
			if (!this->transfers.empty()) {
				auto &buffer = *this->transfers.begin();
				buffer.remove2();
				buffer.completed(buffer.p.transferred);
			}

			// check if we need to start a new transfer
			if (!this->transfers.empty()) {
				auto &buffer = *this->transfers.begin();
				buffer.transfer();
			}
		}
/*		

		if (update()) {
			// end of transfer

			// disable clocks
			//RCC->APB2ENR = RCC->APB2ENR & ~RCC_APB2ENR_SPI1EN;
			//RCC->AHBENR = RCC->AHBENR & ~RCC_AHBENR_DMAEN;

			// check for more transfers
			this->waitlist.visitSecond([this](const SpiMaster::Parameters &p) {
				auto channel = reinterpret_cast<const Channel *>(p.context);
				startTransfer(p.writeData, p.writeCount, p.readData, p.readCount, channel);
			});

			// resume first waiting coroutine
			this->waitlist.resumeFirst([](const SpiMaster::Parameters &p) {
				return true;
			});
		}*/
	}
}
/*
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
*/

// Channel
/*
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
}*/

// BufferBase

SpiMaster_SPI1::BufferBase::BufferBase(uint8_t *data, int size, SpiMaster_SPI1 &master, int csPin, bool dcUsed)
	: coco::Buffer(data, size, BufferBase::State::READY), master(master), csPin(csPin), dcUsed(dcUsed && master.dcPin >= 0)
{
	// configure CS pin: output, high on idle
	gpio::setOutput(csPin, true);
	gpio::configureOutput(csPin);
}

SpiMaster_SPI1::BufferBase::~BufferBase() {	
}

bool SpiMaster_SPI1::BufferBase::start(Op op, int size) {
	if (this->p.state != State::READY || (op & Op::READ_WRITE) == 0) {
		assert(false);
		return false;
	}

	this->op = op;
	this->p.transferred = size;
	this->master.transfers.add(*this);
	
	// start transfer immediately if SPI is idle
	if (DMA1_Channel2->CCR == 0)
		transfer();

	// set state
	setState(State::BUSY);

	return true;
}

void SpiMaster_SPI1::BufferBase::cancel() {
	if (this->p.state == State::BUSY) {
		this->p.transferred = 0;
		setState(State::CANCELLED);
	}
}

void SpiMaster_SPI1::BufferBase::transfer() {
	// check if MISO and DC (data/command) are on the same pin
	if (this->master.sharedPin) {
		if (this->dcUsed) {
			// DC (data/command signal) overrides MISO, i.e. write-only mode
			gpio::configureOutput(this->master.dcPin);
		} else {
			// read/write: no DC signal
			configureAlternateOutput(misoFunction(this->master.dcPin), gpio::Pull::DOWN);
		}
	}

	// enable SPI clock
	RCC->APB2ENR = RCC->APB2ENR | RCC_APB2ENR_SPI1EN;

	int commandCount = std::min(int(this->op & Op::COMMAND_MASK) >> COMMAND_SHIFT, this->p.transferred);
	bool separateCommand = this->dcUsed && commandCount > 0 && commandCount < 15 && commandCount < this->p.transferred;
	bool write2 = (this->op & Op::WRITE) != 0;
	bool write1 = write2 || commandCount > 0;
	bool read = (this->op & Op::READ) != 0;
	
	auto data = intptr_t(this->p.data);
	int count = separateCommand ? commandCount : this->p.transferred;
	DMA1_Channel2->CNDTR = count;
	DMA1_Channel3->CNDTR = count;
	if (write1 && read) {
		// write and read
		DMA1_Channel2->CMAR = data;
		DMA1_Channel3->CMAR = data;
		enableDma();  
	} else if (write1) {
		// write only
		DMA1_Channel2->CMAR = (intptr_t)&this->master.dummy; // read into dummy
		DMA1_Channel3->CMAR = data;
		enableDmaWrite();  
	} else {
		// read only
		DMA1_Channel2->CMAR = data;
		DMA1_Channel3->CMAR = (intptr_t)&this->master.zero; // write zero
		enableDmaRead(); 
	}

	// set DC pin (low: command, high: data)
	if (this->dcUsed)
		gpio::setOutput(this->master.dcPin, commandCount == 0);

	// set CS pin low and store for later
	int csPin = this->csPin;
	gpio::setOutput(csPin, false);
	this->master.csPin = csPin;

	// start SPI
	SPI1->CR1 = this->master.cr1; // -> DMA_ISR_TCIF2

	// prepare second transfer for data
	if (separateCommand) {
		this->master.transfer2 = int(write2) | (int(read) << 1);
		this->master.data = data + commandCount;
		this->master.count = this->p.transferred - commandCount;
	} else {
		this->master.transfer2 = 0;
	}
}

} // namespace coco
