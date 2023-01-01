#pragma once

#include <coco/SpiMaster.hpp>
#include <coco/platform/Handler.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

/**
 * Implementation of SPI hardware interface for nRF52 with multiple virtual channels
 * 
 * Dependencies:
 *
 * Config:
 *	SPI_CONTEXTS: Configuration of SPI channels (can share the same SPI peripheral)
 *
 * Resources:
 *	NRF_SPIM3
 *	GPIO
 *		CS-pins
 */
class SpiMaster_SPIM3 : public Handler {
public:
	/**
	 * Constructor
	 * @param index index of device, 0-3 for NRF_SPIM0 to NRF_SPIM3. Note that only NRF_SPIM3 supports DC pin
	 * @param sckPin clock pin
	 * @param mosiPin master out slave in pin
	 * @param misoPin master in slave out pin
	 * @param dcPin data/command pin e.g. for displays, can be same as MISO for read-only devices
	 */
	SpiMaster_SPIM3(int sckPin, int mosiPin, int misoPin, int dcPin = gpio::DISCONNECTED);

	~SpiMaster_SPIM3() override;

	void handle() override;

	void startTransfer(const SpiMaster::Parameters &p);


	int misoPin;
	bool sharedPin;

	// list for coroutines waiting for transfer to complete
	Waitlist<SpiMaster::Parameters> waitlist;


	/**
	 * Virtual channel to a slave device using a dedicated CS pin
	 */
	class Channel : public SpiMaster {
	public:
		// mode of data/command signal
		enum class Mode {
			NONE,
			COMMAND, // set DC low
			DATA // set DC high
		};

		/**
		 * Constructor
		 * @param device the SPI device to operate on
		 * @param csPin chip select pin of the slave
		 * @param mode mode of data/command pin
		 */
		Channel(SpiMaster_SPIM3 &master, int csPin, Mode mode = Mode::NONE);

		~Channel() override;

		Awaitable<Parameters> transfer(const void *writeData, int writeCount, void *readData, int readCount) override;
		void transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) override;


		SpiMaster_SPIM3 &master;
		int csPin;
		Mode mode;
	};
};

} // namespace coco
