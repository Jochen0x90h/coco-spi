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
    /// @param commandPin Command indicator pin of the display, typically low for command, therefore set gpio::Config::INVERT flag
    /// @param commandPinShared true when multiple displays share the same DC pin
    /// @param commandMask Mask to apply to first byte of header to determine if command pin should b high or low
    /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
    SpiDisplayChannel_SPIM(SpiMaster_SPIM &device, gpio::Config csPin,
        gpio::Config commandPin, bool commandPinShared, uint8_t commandMask,
        spim::Format format);
    ~SpiDisplayChannel_SPIM() override;

protected:
    // Channel methods
    void transferFirst(SpiMaster_SPIM::BufferBase &buffer) override;
    bool transferNext(SpiMaster_SPIM::BufferBase &buffer) override;


    gpio::Config commandPin_;
    bool commandPinShared_;
    uint8_t commandMask_;
};

} // namespace coco
