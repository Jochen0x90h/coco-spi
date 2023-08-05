#include "SpiMaster_cout.hpp"
#include <iostream>


namespace coco {

SpiMaster_cout::SpiMaster_cout(Loop_native &loop, int headerCapacity, int size, std::string name)
	: BufferImpl(new uint8_t[align4(headerCapacity) + size] + align4(headerCapacity), size, State::READY)
	, loop(loop), name(name), headerCapacity(headerCapacity)
{
}

SpiMaster_cout::~SpiMaster_cout() {
	delete [] (this->dat - align4(this->headerCapacity));
}

bool SpiMaster_cout::setHeader(const uint8_t *data, int size) {
	if (size > this->headerCapacity) {
		assert(false);
		return false;
	}

	// copy header before start of buffer data
	std::copy(data, data + size, this->dat - size);
	this->headerSize = size;
	return true;
}

bool SpiMaster_cout::startInternal(int size, Op op) {
	if (this->stat != State::READY) {
		assert(this->stat != State::BUSY);
		return false;
	}

	// check if READ or WRITE flag is set
	assert((op & Op::READ_WRITE) != 0);

	this->xferred = size;
	this->op = op;

	if (!this->inList())
		this->loop.yieldHandlers.add(*this);

	// set state
	setBusy();

	return true;
}

void SpiMaster_cout::cancel() {
	if (this->stat != State::BUSY)
		return;

	// small transfers can be cancelled immeditely, otherwise cancel has no effect (this is arbitrary and only for testing)
	if (this->xferred < 4) {
		remove();
		setReady(0);
	}
}

void SpiMaster_cout::handle() {
	remove();

	std::cout << this->name << ": ";

	int transferred = this->xferred;
	auto op = this->op & Op::READ_WRITE;
	bool allCommand = (this->op & Op::COMMAND) != 0;
	int headerSize = this->headerSize;
	int count;
	if (headerSize > 0 && (/*op != Op::WRITE ||*/ !allCommand)) {
		// need separate header
		std::cout << "header " << headerSize << ' ';
		count = transferred;
	} else {
		count = headerSize + transferred;
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
