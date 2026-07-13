#pragma once

#include "SpiMaster_SPIM.hpp"


namespace coco {

/// @brief Virtual channel to an SPI display such as SSD1306/9.
///
class SpiDisplayChannel_SPIM : public SpiMaster_SPIM::Channel {
public:
    /// @brief Constructor.
    /// @param device The SPI device to operate on
    /// @param csPin Chip select pin of the slave (CS), typically nCS, therefore set gpio::Config::INVERT flag
    /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
    /// @param commandPin Command indicator pin of the display, typically low for command, therefore set gpio::Config::INVERT flag
    /// @param commandPinShared true when multiple displays share the same DC pin
    /// @param commandMask Mask to apply to first byte of header to determine if command pin should b high or low
    SpiDisplayChannel_SPIM(SpiMaster_SPIM &device, gpio::Config csPin, spim::Format format,
        gpio::Config commandPin, bool commandPinShared, uint8_t commandMask);
    ~SpiDisplayChannel_SPIM() override;

protected:
    // Channel methods
    int transferFirst(SpiMaster_SPIM::BufferBase &buffer) override;
    int transferNext(SpiMaster_SPIM::BufferBase &buffer, int steps) override;


    gpio::Config commandPin_;
    bool commandPinShared_;
    uint8_t commandMask_;
};

} // namespace coco
