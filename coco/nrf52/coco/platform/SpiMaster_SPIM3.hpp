#pragma once

#include <coco/BufferImpl.hpp>
#include <coco/Device.hpp>
#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

/**
	Implementation of SPI hardware interface for nRF52 with multiple virtual channels.

	Reference manual:
		https://infocenter.nordicsemi.com/topic/ps_nrf52840/spi.html?cp=5_0_0_5_23
	Resources:
		NRF_SPIM3
		GPIO
			CS-pins
*/
class SpiMaster_SPIM3 : public Loop_RTC0::Handler {
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
		Constructor
		@param loop event loop
		@param sckPin clock pin (SCK)
		@param mosiPin master out slave in pin (MOSI)
		@param misoPin master in slave out pin (MISO)
		@param dcPin data/command pin (DC) e.g. for displays, can be same as MISO for read-only devices
	*/
	SpiMaster_SPIM3(Loop_RTC0 &loop, Speed speed, int sckPin, int mosiPin, int misoPin, int dcPin = gpio::DISCONNECTED);

	~SpiMaster_SPIM3() override;


	class Channel;

	class BufferBase : public BufferImpl, public LinkedListNode, public LinkedListNode2 {
		friend class SpiMaster_SPIM3;
	public:
		/**
			Constructor
			@param data data of the buffer with 8 bytes preceding the data for the header
			@param capacity capacity of the buffer
			@param channel channel to attach to
		*/
		BufferBase(uint8_t *data, int capacity, Channel &channel);
		~BufferBase() override;

		// maximum size of header supported by hardware for DC pin is 14
		bool setHeader(const uint8_t *data, int size) override;
		using BufferImpl::setHeader;
		bool startInternal(int size, Op op) override;
		void cancel() override;

	protected:
		// start transfer
		void transfer();

		Channel &channel;

		int headerSize = 0;
		Op op;
	};

	/**
		Virtual channel to a slave device using a dedicated CS pin
	*/
	class Channel : public Device {
		friend class BufferBase;
	public:
		/**
			Constructor
			@param master the SPI master to operate on
			@param csPin chip select pin of the slave (CS)
			@param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin
		*/
		Channel(SpiMaster_SPIM3 &device, int csPin, bool dcUsed = false);
		~Channel();

		State state() override;
		Awaitable<State> untilState(State state) override;
		int getBufferCount() override;
		BufferBase &getBuffer(int index) override;

	protected:
		// list of buffers
		LinkedList<BufferBase> buffers;

		SpiMaster_SPIM3 &device;
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

	int dcPin;
	bool sharedPin;

	// dummy (state is always READY)
	TaskList<Device::State> stateTasks;

	// list of active transfers
	LinkedList2<BufferBase> transfers;
};

} // namespace coco
