#pragma once

#include <coco/align.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/InterruptQueue.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/spi.hpp>
#include <coco/platform/nvic.hpp>


namespace coco {

/// @brief Implementation of SPI hardware interface for stm32 with multiple virtual channels.
///
/// Resources:
///   SPIx
//      SPI master
///   DMAx
///     RX channel (read)
///     TX channel (write)
///   GPIO
///     CS-pins
class SpiMaster_SPI_DMA {
public:
    /// @brief Constructor for the SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
    /// @param loop Event loop
    /// @param sckPin Clock pin (SCK) and alternate function (see data sheet)
    /// @param mosiPin Master output pin (MOSI) and alternate function (see data sheet), can be NONE
    /// @param misoPin Master input pin (MISO) and alternate function (see data sheet), can be NONE
    /// @param spiInfo Info of SPI instance to use
    /// @param dmaInfo Info of DMA channels to use
    /// @param config SPI configuration
    SpiMaster_SPI_DMA(Loop_Queue &loop, gpio::Config sckPin, gpio::Config mosiPin, gpio::Config misoPin,
        const spi::Info &spiInfo, const dma::DualInfo<> &dmaInfo, spi::Config config = spi::Config::SINGLE_MASTER);


    class Channel;

    // internal buffer base class, derives from IntrusiveListNode for the list of buffers and Loop_Queue::Handler to be notified from the event loop
    class BufferBase : public coco::Buffer, public IntrusiveListNode, public Loop_Queue::CompletionHandler {
        friend class SpiMaster_SPI_DMA;
    public:
        /// @brief Constructor
        /// @param header Header
        /// @param headerCapacity Capacity of the header
        /// @param data Buffer Data
        /// @param capacity Capacity of the buffer
        /// @param channel Channel to attach to
        BufferBase(uint8_t *header, int headerCapacity, uint8_t* data, int capacity, Channel &channel);
        ~BufferBase() override;

        // Buffer methods
        bool start() override;
        bool cancel() override;

    protected:
        void onCompletion() override;

        Channel &channel_;
    };

    using RxChannel = dma::Channel<dma::Mode::PERIPHERAL_TO_MEMORY | dma::Mode::SOURCE_WIDTH_8 | dma::Mode::DESTINATION_DYNAMIC>;
    using TxChannel = dma::Channel<dma::Mode::TX8>;
    struct Registers {
        // spi
        spi::Instance spi;

        // dma
        struct {
            RxChannel rx;
            TxChannel tx;
        } dma;
    };

    /// @brief Virtual channel to a SPI slave device using a dedicated CS pin.
    /// Default implementation transfers header and data as-is
    class Channel : public BufferDevice {
        friend class SpiMaster_SPI_DMA;
        friend class BufferBase;
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), typically nCS, therefore set gpio::Config::INVERT flag
        /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
        Channel(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format);
        ~Channel() override;

        // BufferDevice methods
        int getBufferCount();
        BufferBase &getBuffer(int index);

    protected:
        Registers &registers() {return device_.registers_;}

        // start first transfer and return outstanding steps
        virtual int transferFirst(BufferBase &buffer);

        // start next transfer or return zero if no more steps to do
        virtual int transferNext(BufferBase &buffer, int steps);

        // start transfer of data (header or buffer)
        void start(BufferBase::Op op, volatile void *data, int size);


        SpiMaster_SPI_DMA &device_;
        gpio::Config csPin_;
        spi::Format format_;
        bool eraseSupported_ = false;
        volatile uint8_t dummy_;

        // list of buffers
        IntrusiveList<BufferBase> buffers_;
    };

    /// @brief Buffer for transferring data to/from a SPI slave.
    /// Note that the header may be overwritten when reading data, therefore always set the header before read() or transfer()
    /// @tparam H capacity of header
    /// @tparam B capacity of buffer
    template <int H, int B>
    class Buffer : public BufferBase {
    public:
        Buffer(Channel &channel) : BufferBase(buffer, H, buffer + align4(H), B, channel) {}

    protected:
        alignas(4) uint8_t buffer[align4(H) + align4(B)];
    };


    /// @brief Call from interrupt handler of the RX DMA channel.
    /// First channel of dma::DualInfo, see startup_stm32XXX.c, e.g. extern "C" DMA1_Channel1_IRQHandler()
    void DMA_Rx_IRQHandler();

protected:
    Loop_Queue &loop_;

    Registers registers_;
    int rxDmaIrq_;

    // list of active transfers
    InterruptQueue<BufferBase> transfers_;
};

} // namespace coco
