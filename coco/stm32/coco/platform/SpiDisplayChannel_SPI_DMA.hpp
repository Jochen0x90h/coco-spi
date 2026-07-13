#pragma once

#include "SpiMaster_SPI_DMA.hpp"


namespace coco {

/// @brief Virtual channel to an SPI display such as SSD1306/9.
/// The display has a separate command/data pin to indicate if the transfer is a command or data.
class SpiDisplayChannel_SPI_DMA : public SpiMaster_SPI_DMA::Channel {
public:
    /// @brief Constructor.
    /// @param device The SPI device to operate on
    /// @param csPin Chip select pin of the slave (CS), typically nCS, therefore set gpio::Config::INVERT flag
    /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
    /// @param commandPin Command indicator pin of the display, typically low for command, therefore set gpio::Config::INVERT flag
    /// @param commandPinShared true when command pin is shared with MISO pin
    /// @param commandMask Mask to apply to first byte of header to determine if command pin should b high or low
    SpiDisplayChannel_SPI_DMA(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format,
        gpio::Config commandPin, bool commandPinShared, uint8_t commandMask);
    ~SpiDisplayChannel_SPI_DMA() override;

protected:
    // Channel methods
    int transferFirst(SpiMaster_SPI_DMA::BufferBase &buffer) override;
    int transferNext(SpiMaster_SPI_DMA::BufferBase &buffer, int steps) override;


    gpio::Config commandPin_;
    bool commandPinShared_;
    uint8_t commandMask_;
};

} // namespace coco
