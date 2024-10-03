#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/BufferDevice_cout.hpp>


using namespace coco;

// drivers for SpiMasterTest
struct Drivers {
	Loop_native loop;

	using Device = BufferDevice_cout;
	Device channel1{loop, "channel1", 100ms};
	Device channel2{loop, "channel2", 100ms};
	Device::Buffer buffer1{16, channel1};
	Device::Buffer buffer2{16, channel2};
};

Drivers drivers;
