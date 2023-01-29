#pragma once

#include <coco/SpiMaster.hpp>
#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

/**
 * Implementation of SPI hardware interface for nRF52 with multiple virtual channels.
 * Transfers are not cancellable because a dma transfer may be in progress
 * 
 * Resources:
 *	NRF_SPIM3
 *	GPIO
 *		CS-pins
 */
class SpiMaster_SPIM3 : public Handler {
public:
	enum class Speed : uint32_t {
		K125 = 0x02000000, // 125 kbps
		K250 = 0x04000000, // 250 kbps
 		K500 = 0x08000000, // 500 kbps
		M1 = 0x10000000, // 1 Mbps
		M2 = 0x20000000, // 2 Mbps
		M4 = 0x40000000, // 4 Mbps
		M8 = 0x80000000, // 8 Mbps
		M16 = 0x0A000000, // 16 Mbps
		M32 = 0x14000000 // 32 Mbps
	};

	/**
	 * Constructor
	 * @param loop event loop
	 * @param sckPin clock pin (SCK)
	 * @param mosiPin master out slave in pin (MOSI)
	 * @param misoPin master in slave out pin (MISO)
	 * @param dcPin data/command pin (DC) e.g. for displays, can be same as MISO for read-only devices
	 */
	SpiMaster_SPIM3(Loop_RTC0 &loop, Speed speed, int sckPin, int mosiPin, int misoPin, int dcPin = gpio::DISCONNECTED);

	~SpiMaster_SPIM3() override;

	/**
	 * Virtual channel to a slave device using a dedicated CS pin
	 */
	class Channel : public SpiMaster {
		friend class SpiMaster_SPIM3;
	public:
		// mode of DC signal
		enum class Mode {
			NONE, // device does not have a DC pin
			COMMAND, // set DC pin low, overrides MISO if shared with DC
			DATA // set DC pin high, overrides MISO if shared with DC
		};

		/**
		 * Constructor
		 * @param master the SPI master to operate on
		 * @param csPin chip select pin of the slave
		 * @param mode mode of data/command pin
		 */
		Channel(SpiMaster_SPIM3 &master, int csPin, Mode mode = Mode::NONE);

		~Channel() override;

		[[nodiscard]] Awaitable<Parameters> transfer(const void *writeData, int writeCount, void *readData, int readCount) override;
		void transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) override;

	protected:
		SpiMaster_SPIM3 &master;
		int csPin;
		Mode mode;
	};

protected:
	void handle() override;
	void startTransfer(const void *writeData, int writeCount, void *readData, int readCount, const Channel *channel);

	int misoPin;
	bool sharedPin;

	// list for coroutines waiting for transfer to complete
	Waitlist<SpiMaster::Parameters> waitlist;
};

} // namespace coco
