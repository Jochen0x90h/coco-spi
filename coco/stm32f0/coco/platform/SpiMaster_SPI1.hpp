#pragma once

#include <coco/Buffer.hpp>
#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

/**
 * Implementation of SPI hardware interface for stm32f0 with multiple virtual channels.
 * Transfers are not cancellable because a dma transfer may be in progress
 * 
 * Reference manual: https://www.st.com/resource/en/reference_manual/dm00031936-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * Data sheet: https://www.st.com/resource/en/datasheet/stm32f042f6.pdf
 *
 * Resources:
 *	SPI1: SPI master (reference manual section 28)
 *	DMA1 (reference manual section 10)
 *		Channel2: Read (reference manual table 29)
 *		Channel3: Write
 *	GPIO
 *		CS-pins
 */
class SpiMaster_SPI1 : public Loop_TIM2::Handler {
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
	 * Constructor
	 * @param loop event loop
	 * @param prescaler clock prescaler
	 * @param sckPin clock pin (SCK, PA5 or PB3)
	 * @param mosiPin master out slave in pin (MOSI, PA7 or PB5)
	 * @param misoPin master in slave out pin (MISO, PA6 or PB4)
	 * @param dcPin data/command pin (DC) e.g. for displays, can be same as MISO for write-only devices
	 */
	SpiMaster_SPI1(Loop_TIM2 &loop, Prescaler prescaler, int sckPin, int mosiPin, int misoPin, int dcPin = -1);

	~SpiMaster_SPI1() override;

	/**
	 * Virtual channel to a slave device using a dedicated CS pin
	 */
	/*class Channel : public SpiMaster {
		friend class SpiMaster_SPI1;
	public:
		/ **
		 * Constructor
		 * @param master the SPI master to operate on
		 * @param csPin chip select pin (CS) of the slave
		 * /
		Channel(SpiMaster_SPI1 &master, int csPin);

		~Channel() override;

		[[nodiscard]] Awaitable<Parameters> transfer(void const *writeData, int writeCount, void *readData, int readCount) override;
		void transferBlocking(void const *writeData, int writeCount, void *readData, int readCount) override;

	protected:
		SpiMaster_SPI1 &master;
		int csPin;
	};*/


	class BufferBase : public coco::Buffer, public LinkedListNode2 {
		friend class SpiMaster_SPI1;
	public:
		/**
		 * Constructor
		 * @param master the SPI master to operate on
		 * @param csPin chip select pin of the slave (CS)
		 * @param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin
		 */
		BufferBase(uint8_t *data, int size, SpiMaster_SPI1 &master, int csPin, bool dcUsed);
		~BufferBase() override;

		bool start(Op op, int size) override;
		void cancel() override;

	protected:
		void transfer();

		SpiMaster_SPI1 &master;
		int csPin;
		bool dcUsed;

		Op op;
		//int transferred;
	};

	/**
	 * Buffer for transferring data to/from an endpoint
	 * @tparam N size of buffer
	 */
	template <int N>
	class Buffer : public BufferBase {
	public:
		Buffer(SpiMaster_SPI1 &device, int csPin, bool dcUsed = false) : BufferBase(data, N, device, csPin, dcUsed) {}

	protected:
		uint8_t data[N];
	};

protected:
	void handle() override;
	//void startTransfer(const void *writeData, int writeCount, void *readData, int readCount, const Channel *channel);
	//bool update();

	uint32_t cr1;
	int dcPin;
	bool sharedPin;

	// current CS pin to set high on end of transfer
	int csPin;

	uint8_t dummy;
	uint8_t zero = 0;

	int transfer2;
	intptr_t data;
	int count;

	// list of active transfers
	LinkedList2<BufferBase> transfers;
};

} // namespace coco
