#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/SpiMaster_cout.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
	Loop_native loop;
	SpiMaster_cout transfer{loop, 4, 16, "transfer"};
	SpiMaster_cout commandData{loop, 4, 16, "commandData"};
};
