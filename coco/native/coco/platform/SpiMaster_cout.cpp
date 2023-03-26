#include "SpiMaster_cout.hpp"
#include <iostream>


namespace coco {


SpiMaster_cout::SpiMaster_cout(Loop_native &loop, int capacity, std::string name)
	: Buffer(new uint8_t[capacity], capacity, State::READY), loop(loop), name(name) {
}

SpiMaster_cout::~SpiMaster_cout() {
	delete [] this->p.data;
}

bool SpiMaster_cout::start(Op op) {
	if (this->p.state != State::READY || (op & Op::READ_WRITE) == 0) {
		assert(false);
		return false;
	}

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

	auto op = this->op;
	int transferred = this->p.size;

	std::cout << this->name << ": ";
	if ((op & Op::READ) != 0)
		std::cout << "read ";
	if ((op & Op::WRITE) != 0)
		std::cout << "write ";
	if ((op & Op::COMMAND) != 0)
		std::cout << "command" << (int(op & Op::COMMAND_MASK) >> COMMAND_SHIFT) << ' ';
	std::cout << transferred << std::endl;

	completed(transferred);
}

} // namespace coco
