#include "SpiMaster_SPI1_DMA.hpp"
#include <coco/platform/platform.hpp>
#include <algorithm>


namespace coco {

//using namespace gpio;

namespace {

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
	//| SPI_CR1_DIV8; // is configurable

constexpr int SPI_CR2 = SPI_CR2_DATA_SIZE(8)
	| SPI_CR2_FRXTH
	| SPI_CR2_SSOE; // single master mode


// dma

void enableDma() {
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
/*
void enableDmaRead() {
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
*/
void enableDmaWrite() {
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

} // namespace


// SpiMaster_SPI1_DMA

SpiMaster_SPI1_DMA::SpiMaster_SPI1_DMA(Loop_TIM2 &loop, Prescaler prescaler,
	gpio::PinFunction sckPin, gpio::PinFunction mosiPin, gpio::PinFunction misoPin, int dcPin)
	: cr1(SPI_CR1 | (int(prescaler) << SPI_CR1_BR_Pos)), misoFunction(misoPin.function), dcPin(dcPin), sharedPin(misoPin.pin == dcPin)
{
	// configure SPI pins (driven low when SPI is disabled)
	configureAlternateOutput(sckPin);
	configureAlternateOutput(mosiPin);
	configureAlternateInput(misoPin);

	// configure DC pin if used: output
	if (dcPin >= 0 && dcPin != misoPin.pin)
		gpio::configureOutput(dcPin);

	// enable clock and initialize DMA
	RCC->AHBENR = RCC->AHBENR | RCC_AHBENR_DMAEN;
	DMA1_Channel2->CPAR = (uint32_t)&SPI1->DR; // data register
	DMA1_Channel3->CPAR = (uint32_t)&SPI1->DR;

	// add to list of handlers
	loop.handlers.add(*this);
}

SpiMaster_SPI1_DMA::~SpiMaster_SPI1_DMA() {
}

void SpiMaster_SPI1_DMA::handle() {
	// check if read DMA has completed
	if ((DMA1->ISR & DMA_ISR_TCIF2) != 0) {
		// clear interrupt flags at DMA and NVIC
		DMA1->IFCR = DMA_IFCR_CTCIF2;
		clearInterrupt(DMA1_Ch2_3_DMA2_Ch1_2_IRQn);

		auto op = this->transfer2;
		if (op != coco::Buffer::Op::NONE) {
			this->transfer2 = coco::Buffer::Op::NONE;

			// disable SPI and DMA
			SPI1->CR1 = 0;
			DMA1_Channel2->CCR = 0;
			DMA1_Channel3->CCR = 0;

			auto current = this->transfers.begin();
			if (current != this->transfers.end()) {
				auto &buffer = *current;

				// set DC pin high to indicate data or keep low when everything is a command
				if (buffer.channel.dcUsed && (buffer.op & coco::Buffer::Op::COMMAND) == 0)
					gpio::setOutput(this->dcPin, true);

				auto data = intptr_t(buffer.p.data);
				int count = buffer.p.size;
				DMA1_Channel2->CNDTR = count;
				DMA1_Channel3->CNDTR = count;
				if (op == coco::Buffer::Op::WRITE) {
					// write only
					DMA1_Channel2->CMAR = (intptr_t)&this->dummy; // read into dummy
					DMA1_Channel3->CMAR = data;
					enableDmaWrite();
				}/* else if (op == coco::Buffer::Op::READ) {
					// read only
					DMA1_Channel2->CMAR = data;
					DMA1_Channel3->CMAR = (intptr_t)&this->zero; // write zero
					enableDmaRead();
				}*/ else {
					// write and read
					DMA1_Channel2->CMAR = data;
					DMA1_Channel3->CMAR = data;
					enableDma();
				}

			}

			// start SPI for second transfer
			SPI1->CR1 = this->cr1; // -> DMA_ISR_TCIF2
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

			auto current = this->transfers.begin();
			if (current != this->transfers.end()) {
				// check if more transfers are pending
				auto next = current;
				++next;
				if (next != this->transfers.end())
					next->transfer();

				// set current buffer to ready state and resume waiting coroutines
				auto &buffer = *current;
				buffer.remove2();
				buffer.setReady();
			}
		}
	}
}


// Channel

SpiMaster_SPI1_DMA::Channel::Channel(SpiMaster_SPI1_DMA &master, int csPin, bool dcUsed)
	: master(master), csPin(csPin), dcUsed(dcUsed && master.dcPin >= 0)
{
	// configure CS pin: output, high on idle
	gpio::setOutput(csPin, true);
	gpio::configureOutput(csPin);
}

SpiMaster_SPI1_DMA::Channel::~Channel() {
}

int SpiMaster_SPI1_DMA::Channel::getBufferCount() {
	return this->buffers.count();
}

HeaderBuffer &SpiMaster_SPI1_DMA::Channel::getBuffer(int index) {
	return this->buffers.get(index);
}


// BufferBase

SpiMaster_SPI1_DMA::BufferBase::BufferBase(uint8_t *data, int capacity, Channel &channel)
	: HeaderBuffer(data, capacity, BufferBase::State::READY), channel(channel)
{
	channel.buffers.add(*this);
}

SpiMaster_SPI1_DMA::BufferBase::~BufferBase() {
}

void SpiMaster_SPI1_DMA::BufferBase::setHeader(const uint8_t *data, int size) {
	// copy header before start of buffer data
	std::copy(data, data + size, this->p.data - size);
	this->headerSize = size;
}

bool SpiMaster_SPI1_DMA::BufferBase::start(Op op) {
	assert(this->p.state == State::READY && (op & Op::READ_WRITE) != 0);
	auto &master = this->channel.master;

	this->op = op;

	// add to list of pending transfers
	this->inProgress = false;
	master.transfers.add(*this);

	// start transfer immediately if SPI is idle
	if (DMA1_Channel2->CCR == 0)
		transfer();

	// set state
	setState(State::BUSY);

	return true;
}

void SpiMaster_SPI1_DMA::BufferBase::cancel() {
	if (this->p.state == State::BUSY) {
		this->p.size = 0;
		setState(State::CANCELLED);

		// can be cancelled immediately if not yet in progress
		if (!this->inProgress) {
			remove2();
			setState(State::READY);
		}
	}
}

void SpiMaster_SPI1_DMA::BufferBase::transfer() {
	this->inProgress = true;
	auto &master = this->channel.master;

	// check if MISO and DC (data/command) are on the same pin
	if (master.sharedPin) {
		if (this->channel.dcUsed) {
			// DC (data/command signal) overrides MISO, i.e. write-only mode
			gpio::configureOutput(master.dcPin);
		} else {
			// read/write: no DC signal
			gpio::configureAlternateInput({master.dcPin, master.misoFunction}, gpio::Pull::DOWN);
		}
	}

	// enable SPI clock
	RCC->APB2ENR = RCC->APB2ENR | RCC_APB2ENR_SPI1EN;

	auto op = this->op & Op::READ_WRITE;
	bool allCommand = (this->op & Op::COMMAND) != 0;
	int headerSize = this->headerSize;
	auto data = intptr_t(this->p.data - headerSize);
	if (headerSize > 0 && (/*op != Op::WRITE ||*/ (!allCommand && this->channel.dcUsed))) {
		// need separate header
		DMA1_Channel2->CNDTR = headerSize;
		DMA1_Channel3->CNDTR = headerSize;

		// write only
		DMA1_Channel2->CMAR = (intptr_t)&master.dummy; // read into dummy
		DMA1_Channel3->CMAR = data;
		enableDmaWrite();

		// prepare second transfer
		master.transfer2 = op;
	} else {
		int count = headerSize + this->p.size;
		DMA1_Channel2->CNDTR = count;
		DMA1_Channel3->CNDTR = count;
		if (op == Op::WRITE) {
			// write only
			DMA1_Channel2->CMAR = (intptr_t)&master.dummy; // read into dummy
			DMA1_Channel3->CMAR = data;
			enableDmaWrite();
		}/* else if (op == Op::READ) {
			// read only
			DMA1_Channel2->CMAR = data;
			DMA1_Channel3->CMAR = (intptr_t)&master.zero; // write zero
			enableDmaRead();
		}*/ else {
			// write and read
			DMA1_Channel2->CMAR = data;
			DMA1_Channel3->CMAR = data;
			enableDma();
		}

		// no second transfer
		master.transfer2 = Op::NONE;
	}

	// set DC pin (low: command, high: data)
	if (this->channel.dcUsed)
		gpio::setOutput(master.dcPin, !(this->headerSize > 0 || allCommand));

	// set CS pin low and store for later
	int csPin = this->channel.csPin;
	gpio::setOutput(csPin, false);
	master.csPin = csPin;

	// start SPI
	SPI1->CR1 = master.cr1; // -> DMA_ISR_TCIF2
}

} // namespace coco
