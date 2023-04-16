#pragma once

#include <coco/HeaderBufferList.hpp>
#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/gpio.hpp>


namespace coco {

/**
	Implementation of SPI hardware interface for nRF52 with multiple virtual channels.

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


	class BufferBase;

	/**
		Virtual channel to a slave device using a dedicated CS pin
	*/
	class Channel : public HeaderBufferList {
		friend class BufferBase;
	public:
		/**
			Constructor
			@param master the SPI master to operate on
			@param csPin chip select pin of the slave (CS)
			@param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin
		*/
		Channel(SpiMaster_SPIM3 &master, int csPin, bool dcUsed = false);
		~Channel();

		int getBufferCount();
		HeaderBuffer &getBuffer(int index);

	protected:
		// list of buffers
		LinkedList<BufferBase> buffers;

		SpiMaster_SPIM3 &master;
		int csPin;
		bool dcUsed;
	};


	class BufferBase : public HeaderBuffer, public LinkedListNode, public LinkedListNode2 {
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
		void setHeader(const uint8_t *data, int size) override;
		bool start(Op op) override;
		void cancel() override;

	protected:
		// start transfer
		void transfer();

		Channel &channel;

		int headerSize = 0;
		Op op;
		bool inProgress;
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

	// list of active transfers
	LinkedList2<BufferBase> transfers;
};

} // namespace coco
