#include "SpiMaster_cout.hpp"
#include <iostream>


namespace coco {

SpiMaster_cout::SpiMaster_cout(Loop_native &loop, int headerCapacity, int capacity, std::string name)
	: HeaderBuffer(new uint8_t[align4(headerCapacity) + capacity] + align4(headerCapacity), capacity, State::READY)
	, loop(loop), name(name), headerCapacity(headerCapacity)
{
}

SpiMaster_cout::~SpiMaster_cout() {
	delete [] (this->p.data - align4(this->headerCapacity));
}

void SpiMaster_cout::setHeader(const uint8_t *data, int size) {
	assert(size <= this->headerCapacity);

	// copy header before start of buffer data
	std::copy(data, data + size, this->p.data - size);
	this->headerSize = size;
}

bool SpiMaster_cout::start(Op op) {
	assert(this->p.state == State::READY && (op & Op::READ_WRITE) != 0);

	this->op = op;

	if (!this->inList())
		this->loop.yieldHandlers.add(*this);

	// set state
	setState(State::BUSY);

	return true;
}

void SpiMaster_cout::cancel() {
	if (this->p.state == State::BUSY) {
		this->p.size = 0;
		setState(State::CANCELLED);
	}
}

void SpiMaster_cout::handle() {
	this->remove();

	std::cout << this->name << ": ";

	auto op = this->op & Op::READ_WRITE;
	bool allCommand = (this->op & Op::COMMAND) != 0;
	int headerSize = this->headerSize;
	int count;
	if (headerSize > 0 && (/*op != Op::WRITE ||*/ !allCommand)) {
		// need separate header
		std::cout << "header " << headerSize << ' ';
		count = this->p.size;
	} else {
		count = headerSize + this->p.size;
	}

	if ((op & Op::READ) != 0)
		std::cout << "read ";
	if ((op & Op::WRITE) != 0)
		std::cout << "write ";
	if (allCommand)
		std::cout << "command ";
	std::cout << count << std::endl;

	setReady();
}

} // namespace coco
