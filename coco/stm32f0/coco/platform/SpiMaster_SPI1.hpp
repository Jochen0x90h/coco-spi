#pragma once

#include <coco/SpiMaster.hpp>
#include <coco/platform/Handler.hpp>
#include <cstdint>


namespace coco {

/**
 * Implementation of SPI hardware interface for stm32f0 with multiple virtual channels
 * 
 * Reference manual: https://www.st.com/resource/en/reference_manual/dm00031936-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * Data sheet: https://www.st.com/resource/en/datasheet/stm32f042f6.pdf
 *
 * Dependencies:
 *
 * Config:
 *	SPI_CONTEXTS: Configuration of SPI channels (can share the same SPI peripheral)
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
	/**
	 * Constructor
	 * @param index index of device, 1-2 for SPI1 or SPI2
	 * @param sckPin clock pin (SPI1: PA5 or PB3)
	 * @param mosiPin master out slave in pin (SPI1: PA7 or PB5)
	 * @param misoPin master in slave out pin (SPI1: PA6 or PB4)
	 */
	SpiMaster_SPI1(int index, int sckPin, int mosiPin, int misoPin);

	~SpiMaster_SPI1() override;

	void handle() override;

	/**
	 * Virtual channel to a slave device using a dedicated CS pin
	 */
	class Channel : public SpiMaster {
		friend class SpiMaster_SPI1;
	public:
		/**
		 * Constructor
		 * @param device the SPI device to operate on
		 * @param csPin chip select pin of the slave
		 */
		Channel(SpiMaster_SPI1 &master, int csPin);

		~Channel() override;

		Awaitable<Parameters> transfer(void const *writeData, int writeCount, void *readData, int readCount) override;
		void transferBlocking(void const *writeData, int writeCount, void *readData, int readCount) override;

	protected:
		SpiMaster_SPI1 &master;
		int csPin;
	};

protected:
	void startTransfer(SpiMaster::Parameters const &p);

	bool update();

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
