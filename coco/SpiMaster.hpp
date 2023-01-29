#pragma once

#include <coco/Coroutine.hpp>


namespace coco {

/**
 * Interface to an SPI device.
 * If it is possible to cancel a coroutine that co_awaits completion of transfer() depends on the implementation
 */
class SpiMaster {
public:

	// Internal helper: Stores the parameters in the awaitable during co_await
	struct Parameters {
		// write data
		void const *writeData;
		int writeCount;

		// read data
		void *readData;
		int readCount;

		// context, e.g. the virtual channel the data belongs to
		void *context;
	};


	virtual ~SpiMaster();

	/**
	 * Transfer data to/from SPI device. Zero length transfers are not supported, i.e. writeCount or readCount must be
	 * greater than zero.
	 * @param writeData data to write (driver may require that the data is located in RAM)
	 * @param writeCount number of bytes to write
	 * @param readData data to read
	 * @param readCount number of bytes to read
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] virtual Awaitable<Parameters> transfer(void const *writeData, int writeCount, void *readData, int readCount) = 0;

	/**
	 * Write to an SPI device, convenience method.
	 * @param data data to write (driver may require that the data is located in RAM)
	 * @param count number of bytes to write
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] inline Awaitable<Parameters> write(void const *data, int count) {
		return transfer(data, count, nullptr, 0);
	}

	/**
	 * Transfer data to/from SPI device and block until finished. Zero length transfers are not supported, i.e.
	 * writeCount or readCount must be greater than zero.
	 * @param writeData data to write (driver may require that the data is located in RAM)
	 * @param writeCount number of bytes to write
	 * @param readData data to read
	 * @param readCount number of bytes to read
	 */
	virtual void transferBlocking(void const *writeData, int writeCount, void *readData, int readCount) = 0;
};

} // namespace coco
