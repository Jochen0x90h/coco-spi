#pragma once

#include <coco/SpiMaster.hpp>
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
class SpiMaster_SPI1 : public Handler {
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
	 */
	SpiMaster_SPI1(Loop_TIM2 &loop, Prescaler prescaler, int sckPin, int mosiPin, int misoPin);

	~SpiMaster_SPI1() override;

	/**
	 * Virtual channel to a slave device using a dedicated CS pin
	 */
	class Channel : public SpiMaster {
		friend class SpiMaster_SPI1;
	public:
		/**
		 * Constructor
		 * @param master the SPI master to operate on
		 * @param csPin chip select pin (CS) of the slave
		 */
		Channel(SpiMaster_SPI1 &master, int csPin);

		~Channel() override;

		[[nodiscard]] Awaitable<Parameters> transfer(void const *writeData, int writeCount, void *readData, int readCount) override;
		void transferBlocking(void const *writeData, int writeCount, void *readData, int readCount) override;

	protected:
		SpiMaster_SPI1 &master;
		int csPin;
	};

protected:
	void handle() override;
	void startTransfer(const void *writeData, int writeCount, void *readData, int readCount, const Channel *channel);
	bool update();

	uint32_t cr1;

	uint32_t readAddress;
	int readCount;
	uint32_t writeAddress;
	int writeCount;
	uint8_t readDummy;
	uint8_t writeDummy = 0;

	int csPin;
	Waitlist<SpiMaster::Parameters> waitlist;
};

} // namespace coco
