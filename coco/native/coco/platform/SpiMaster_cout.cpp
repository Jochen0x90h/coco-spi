#include "SpiMaster_cout.hpp"
#include <iostream>


namespace coco {

SpiMaster_cout::~SpiMaster_cout() {
}

Awaitable<SpiMaster::Parameters> SpiMaster_cout::transfer(const void *writeData, int writeCount, void *readData, int readCount) {
	if (!inList()) {
		coco::yieldHandlers.add(*this);
	}
	return {this->waitlist, nullptr, writeData, writeCount, readData, readCount};
}

void SpiMaster_cout::transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) {
	std::cout << this->name << ' ' << writeCount << ' ' << readCount << std::endl;
}

void SpiMaster_cout::activate() {
	this->remove();

	// resume all coroutines
	this->waitlist.resumeAll([this](Parameters &p) {
		transferBlocking(p.writeData, p.writeCount, p.readData, p.readCount);
		return true;
	});
}

} // namespace coco
