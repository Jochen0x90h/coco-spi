#pragma once

#include <coco/align.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/dma.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/spi.hpp>
#include <coco/platform/nvic.hpp>


namespace coco {

/**
 * Implementation of SPI hardware interface for stm32f0 with multiple virtual channels.
 *
 * Reference manual:
 *   f0:
 *     https://www.st.com/resource/en/reference_manual/dm00031936-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 *       SPI: section 28
 *       DMA: section 10, table 29
 *       Code Examples: section A.17
 *   f3:
 *     https://www.st.com/resource/en/reference_manual/rm0364-stm32f334xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 *       SPI: section 29
 *       DMA: section 11, table 31
 *   g4:
 *     https://www.st.com/resource/en/reference_manual/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 *       SPI: section 39
 *       DMA: section 12
 *       DMAMUX: section 13
 * Data sheet:
 *   f0:
 *     https://www.st.com/resource/en/datasheet/stm32f042f6.pdf
 *       Alternate Functions: Section 4, Tables 14-16, Page 37
 *     https://www.st.com/resource/en/datasheet/dm00039193.pdf
 *       Alternate Functions: Section 4, Tables 14+15, Page 37
 *   f3:
 *     https://www.st.com/resource/en/datasheet/stm32f334k4.pdf
 *       Alternate Functions: Section 4, Table 14, Page 42
 *   g4:
 *     https://www.st.com/resource/en/datasheet/stm32g431rb.pdf
 *       Alternate Functions: Section 4.11, Table 13, Page 61
 * Resources:
 *   SPIx: SPI master
 *   DMAx
 *     RX channel (read)
 *     TX channel (write)
 *   GPIO
 *     CS-pins
 */
class SpiMaster_SPI_DMA {
public:
    /**
     * Constructor for the SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
     * @param loop event loop
     * @param sckPin clock pin, port and alternate function (SCK, see data sheet)
     * @param misoPin master in / slave out pin and alternate function (MISO, see data sheet), can be NONE
     * @param mosiPin master out / slave in pin and alternate function (MOSI, see data sheet), can be NONE
     * @param spiInfo info of SPI instance to use
     * @param dmaInfo info of DMA channels to use
     * @param prescaler clock prescaler
     */
    SpiMaster_SPI_DMA(Loop_Queue &loop, gpio::Config sckPin, gpio::Config misoPin, gpio::Config mosiPin,
        const spi::Info &spiInfo, const dma::Info2 &dmaInfo, spi::Config config)
        : SpiMaster_SPI_DMA(loop, sckPin, misoPin, mosiPin, gpio::Config::NONE, spiInfo, dmaInfo, config)
    {}

    /**
     * Constructor for the SPI device with data/command (DC) support.
     * @param loop event loop
     * @param sckPin clock pin, port and alternate function (SCK, see data sheet)
     * @param misoPin master in / slave out pin and alternate function (MISO, see data sheet), can be NONE
     * @param mosiPin master out / slave in pin and alternate function (MOSI, see data sheet), can be NONE
     * @param dcPin data/command pin (DC) e.g. for displays, can be same as MISO for write-only devices
     * @param spiInfo info of SPI instance to use
     * @param dmaInfo info of DMA channels to use
     * @param prescaler clock prescaler
     */
    SpiMaster_SPI_DMA(Loop_Queue &loop,
        gpio::Config sckPin, gpio::Config misoPin, gpio::Config mosiPin, gpio::Config dcPin,
        const spi::Info &spiInfo, const dma::Info2 &dmaInfo, spi::Config config);


    class Channel;

    // internal buffer base class, derives from IntrusiveListNode for the list of buffers and Loop_Queue::Handler to be notified from the event loop
    class BufferBase : public coco::Buffer, public IntrusiveListNode, public Loop_Queue::Handler {
        friend class SpiMaster_SPI_DMA;
    public:
        /**
         * Constructor
         * @param data data of the buffer
         * @param capacity capacity of the buffer
         * @param channel channel to attach to
         */
        BufferBase(uint8_t *data, int capacity, Channel &channel);
        ~BufferBase() override;

        // Buffer methods
        bool start(Op op) override;
        bool cancel() override;

    protected:
        void start();
        void handle() override;

        Channel &channel;

        Op op;
    };

    /**
     * Virtual channel to a SPI slave device using a dedicated CS pin
     */
    class Channel : public BufferDevice {
        friend class SpiMaster_SPI_DMA;
        friend class BufferBase;
    public:
        /**
         * Constructor
         * @param device the SPI device to operate on
         * @param csPin chip select pin of the slave (CS), typically nCS, therefore set INVERT flag
         * @param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin
         */
        Channel(SpiMaster_SPI_DMA &device, gpio::Config csPin, bool dcUsed = false);
        ~Channel();

        // BufferDevice methods
        int getBufferCount();
        BufferBase &getBuffer(int index);

    protected:
        // list of buffers
        IntrusiveList<BufferBase> buffers;

        SpiMaster_SPI_DMA &device;
        gpio::Config csPin;
        bool dcUsed;
    };

    /**
     * Buffer for transferring data to/from a SPI slave.
     * Note that the header may get overwritten when reading data, therefore always set the header before read() or transfer()
     * @tparam C capacity of buffer
     */
    template <int C>
    class Buffer : public BufferBase {
    public:
        Buffer(Channel &channel) : BufferBase(data, C, channel) {}

    protected:
        alignas(4) uint8_t data[C];
    };

    /**
     * Call from interrupt handler for the RX DMA channel (first channel of dma::DualChannel)
     */
    void DMA_Rx_IRQHandler();

protected:
    Loop_Queue &loop;

    // pins
    gpio::Config dcPin;
    bool sharedPin; // set if DC and MISO share the same pin

    // spi
    SPI_TypeDef *spi;

    // dma
    dma::Status rxStatus;
    dma::Channel rxChannel;
    int rxDmaIrq;
    dma::Status txStatus;
    dma::Channel txChannel;
    uint8_t dummy;

    BufferBase::Op transfer2;

    // list of active transfers
    InterruptQueue<BufferBase> transfers;
};

} // namespace coco
