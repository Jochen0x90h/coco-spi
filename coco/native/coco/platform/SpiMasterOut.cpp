#include "SpiMasterOut.hpp"
#include <coco/loop.hpp>
#include <iostream>


namespace coco {

SpiMasterOut::~SpiMasterOut() {
}

Awaitable<SpiMaster::Parameters> SpiMasterOut::transfer(const void *writeData, int writeCount, void *readData, int readCount) {
	if (!isInList()) {
		this->time = loop::now() + 100ms; // emulate 100ms transfer time
		coco::timeHandlers.add(*this);
	}
	return {this->waitlist, nullptr, writeData, writeCount, readData, readCount};
}

void SpiMasterOut::transferBlocking(const void *writeData, int writeCount, void *readData, int readCount) {
	std::cout << this->name << ' ' << writeCount << ' ' << readCount << std::endl;
}

void SpiMasterOut::activate() {
	this->remove();

	// resume all coroutines
	this->waitlist.resumeAll([this](Parameters &p) {
		transferBlocking(p.writeData, p.writeCount, p.readData, p.readCount);
		return true;
	});
}

} // namespace coco
