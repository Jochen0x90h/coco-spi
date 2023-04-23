#pragma once

#include <coco/BufferImpl.hpp>
#include <coco/Device.hpp>
#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

/**
	Implementation of SPI hardware interface for stm32f0 with multiple virtual channels.

	Reference manual:
		f0:
			https://www.st.com/resource/en/reference_manual/dm00031936-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
				SPI: section 28
				DMA: section 10, table 29
				Code Examples: section A.17
		g4:
			https://www.st.com/resource/en/reference_manual/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
				SPI: section 39
	Data sheet:
		f0:
			https://www.st.com/resource/en/datasheet/stm32f042f6.pdf
		g4:
			https://www.st.com/resource/en/datasheet/stm32g431rb.pdf

	Resources:
		SPI1: SPI master
		DMA1
			Channel2: RX (read)
			Channel3: TX (write)
		GPIO
			CS-pins
*/
class SpiMaster_SPI1_DMA : public Loop_TIM2::Handler {
public:
	enum class Prescaler {
		DIV2 = 0,
		DIV4 = 1,
		DIV8 = 2,
		DIV16 = 3,
		DIV32 = 4,
		DIV64 = 5,
		DIV128 = 6,
		DIV256 = 7
	};

	/**
		Constructor
		@param loop event loop
		@param prescaler clock prescaler
		@param sckPin clock pin and alternate function (SCK, see data sheet)
		@param mosiPin master out slave in pin and alternate function (MOSI, see data sheet)
		@param misoPin master in slave out pin and alternate function (MISO, see data sheet)
		@param dcPin data/command pin (DC) e.g. for displays, can be same as MISO for write-only devices
	*/
	SpiMaster_SPI1_DMA(Loop_TIM2 &loop, Prescaler prescaler, gpio::PinFunction sckPin, gpio::PinFunction mosiPin, gpio::PinFunction misoPin, int dcPin = -1);

	~SpiMaster_SPI1_DMA() override;


	class Channel;

	class BufferBase : public BufferImpl, public LinkedListNode, public LinkedListNode2 {
		friend class SpiMaster_SPI1_DMA;
	public:
		/**
			Constructor
			@param data data of the buffer with 8 bytes preceding the data for the header
			@param capacity capacity of the buffer
			@param channel channel to attach to
		*/
		BufferBase(uint8_t *data, int capacity, Channel &channel);
		~BufferBase() override;

		bool setHeader(const uint8_t *data, int size) override;
		using BufferImpl::setHeader;
		bool startInternal(int size, Op op) override;
		void cancel() override;

	protected:
		void transfer();

		Channel &channel;

		int headerSize = 0;
		Op op;
		bool inProgress;
	};

	/**
		Virtual channel to a slave device using a dedicated CS pin
	*/
	class Channel : public Device {
		friend class SpiMaster_SPI1_DMA;
		friend class BufferBase;
	public:
		/**
			Constructor
			@param master the SPI master to operate on
			@param csPin chip select pin of the slave (CS)
			@param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin
		*/
		Channel(SpiMaster_SPI1_DMA &master, int csPin, bool dcUsed = false);
		~Channel();

		State state() override;
		Awaitable<State> untilState(State state) override;
		int getBufferCount();
		BufferBase &getBuffer(int index);

	protected:
		// list of buffers
		LinkedList<BufferBase> buffers;

		SpiMaster_SPI1_DMA &master;
		int csPin;
		bool dcUsed;
	};

	/**
		Buffer for transferring data to/from a SPI slave.
		Note that the header may get overwritten when reading data, therefore always set the header before read() or transfer()
		@tparam H capacity of header
		@tparam B capacity of buffer
	*/
	template <int H, int B>
	class Buffer : public BufferBase {
	public:
		Buffer(Channel &channel) : BufferBase(data + align4(H), B, channel) {}

	protected:
		alignas(4) uint8_t data[align4(H) + B];
	};

protected:
	void handle() override;

	uint32_t cr1;
	int misoFunction;
	int dcPin;
	bool sharedPin;

	// current CS pin to set high on end of transfer
	int csPin;

	uint8_t dummy;
	uint8_t zero = 0;

	coco::Buffer::Op transfer2;

	// dummy
	TaskList<Device::State> stateTasks;

	// list of active transfers
	LinkedList2<BufferBase> transfers;
};

} // namespace coco
