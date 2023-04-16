#include <coco/HeaderBuffer.hpp>
#include <coco/platform/Loop_native.hpp>
#include <string>


namespace coco {

/**
	Implementation of an SPI master that simply writes info about the transfer operations to std::cout
*/
class SpiMaster_cout : public HeaderBuffer, public Loop_native::YieldHandler {
	friend class SpiMaster_cout;
public:
	/**
		Constructor
		@param loop event loop
		@param capacity buffer capacity
		@param name name for printing
	*/
	SpiMaster_cout(Loop_native &loop, int headerCapacity, int capacity, std::string name);
	~SpiMaster_cout() override;

	void setHeader(const uint8_t *data, int size) override;
	bool start(Op op) override;
	void cancel() override;

protected:
	void handle() override;

	Loop_native &loop;
	std::string name;

	int headerCapacity;
	int headerSize = 0;
	Op op;
};

} // namespace coco
