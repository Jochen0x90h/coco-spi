#include <coco/Buffer.hpp>
#include <coco/platform/Loop_native.hpp>
#include <string>


namespace coco {

class SpiMaster_cout : public Buffer, public Loop_native::YieldHandler {
	friend class SpiMaster_cout;
public:
	/**
	 * Constructor
	 * @param master the SPI master to operate on
	 * @param csPin chip select pin of the slave (CS)
	 * @param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin
	 */
	SpiMaster_cout(Loop_native &loop, int size, std::string name);
	~SpiMaster_cout() override;

	bool start(Op op, int size) override;
	void cancel() override;

protected:
	void handle() override;

	Loop_native &loop;
	std::string name;

	Op op;
	//int transferred;
};

} // namespace coco
