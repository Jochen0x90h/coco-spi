#include <coco/SpiMaster.hpp>
#include <coco/platform/Handler.hpp>
#include <string>


namespace coco {

/**
 * Implementation of an SPI master that simply writes info about the transfer operations to std::cout
 */
class SpiMaster_cout : public SpiMaster, public YieldHandler {
public:
	/**
	 * Constructor
	 * @param name name of the SPI master that appears in the printed messages
	 */
	explicit SpiMaster_cout(std::string name) : name(std::move(name)) {
	}

	~SpiMaster_cout() override;

	[[nodiscard]] Awaitable<Parameters> transfer(const void *writeData, int writeCount, void *readData, int readCount) override;
	void transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) override;

	void activate() override;

protected:
	std::string name;
	Waitlist<SpiMaster::Parameters> waitlist;
};

} // namespace coco
