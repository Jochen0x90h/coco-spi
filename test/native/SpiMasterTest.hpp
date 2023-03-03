#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/SpiMaster_cout.hpp>


using namespace coco;

// drivers for SpiTest
struct Drivers {
	Loop_native loop;
	SpiMaster_cout transfer{loop, 16, "transfer"};
	SpiMaster_cout commandData{loop, 16, "commandData"};
};
