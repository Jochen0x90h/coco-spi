#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/SpiMaster_cout.hpp>


using namespace coco;

// drivers for SpiTest
struct Drivers {
	Loop_native loop;
	SpiMaster_cout transfer{loop, "transfer"};
	SpiMaster_cout command{loop, "command"};
	SpiMaster_cout data{loop, "data"};
};
