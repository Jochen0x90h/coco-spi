#pragma once

#include <coco/align.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/InterruptQueue.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/qspi.hpp>
#include <coco/platform/nvic.hpp>


#ifdef HAVE_QUADSPI
namespace coco {

/// @brief Quad SPI master stm32 with multiple virtual channels for accessing external memory chips.
///
/// Resources:
///   QUADSPI
//      SPI master
///   DMAx
///     RX channel (read)
///     TX channel (write)
///   GPIO
///     CS-pins
class SpiMemory_QUADSPI_DMA {
public:
    /// @brief Constructor for the quad SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
    /// @param loop Event loop
    /// @param sckPin Clock pin (SCK) and alternate function (see data sheet)
    /// @param sio0Pin Serial IO pin 0 or master output pin (MOSI) and alternate function (see data sheet)
    /// @param sio1Pin Serial IO pin 1 or master input pin (MISO) and alternate function (see data sheet)
    /// @param sio2Pin Serial IO pin 2 and alternate function (see data sheet), can be NONE
    /// @param sio3Pin Serial IO pin 3 and alternate function (see data sheet), can be NONE
    /// @param quadspiInfo Info of QUADSPI instance to use
    /// @param dmaInfo Info of DMA channel to use
    /// @param config QUADSPI config (prescaler, fifo threshold)
    SpiMemory_QUADSPI_DMA(Loop_Queue &loop, Array<const gpio::Config> pins,
        const qspi::Info &qspiInfo, const dma::Info<> &dmaInfo);


    class Channel;

    // internal buffer base class, derives from IntrusiveListNode for the list of buffers and Loop_Queue::Handler to be notified from the event loop
    class BufferBase : public coco::Buffer, public IntrusiveListNode, public Loop_Queue::Handler {
        friend class SpiMemory_QUADSPI_DMA;
    public:
        /// @brief Constructor
        /// @param headerAndData Header (4 bytes) and data of the buffer
        /// @param capacity Capacity of the buffer
        /// @param channel Channel to attach to
        BufferBase(uint8_t *headerAndData, int capacity, Channel &channel);
        ~BufferBase() override;

        // Buffer methods
        bool start() override;
        bool cancel() override;

        //Op op() {return op_;}
        //void setOp(Op op) {op_ = op;}
    protected:
        //void start();
        void handle() override;

        Channel &channel_;
        //Op op_;
    };

    using RxChannel = dma::Channel<dma::Mode::RX8>;
    using TxChannel = dma::Channel<dma::Mode::TX8>;
    struct Registers {
        // quad spi
        qspi::Instance qspi;

        // dma channels
        union {
            RxChannel rx;
            TxChannel tx;
        } dma;
    };

    /// @brief Virtual channel to a SPI slave device using a dedicated CS pin.
    ///
    class Channel : public BufferDevice {
        friend class SpiMemory_QUADSPI_DMA;
        friend class BufferBase;
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), typically nCS, therefore set INVERT flag
        Channel(SpiMemory_QUADSPI_DMA &device, gpio::Config csPin,
            qspi::Format format, qspi::CommFormat commFormat,
            uint8_t readCommand, uint8_t writeEnableCommand, uint8_t writeCommand, uint8_t eraseCommand, uint8_t readStatusCommand);
        ~Channel() override;

        // BufferDevice methods
        int getBufferCount();
        BufferBase &getBuffer(int index);

    protected:
        Registers &registers() {return device_.registers_;}
        auto &op(BufferBase &buffer) {return buffer.op_;}

        // start first transfer
        virtual int transferFirst(BufferBase &buffer);

        // start next transfer or return false if no more transfers are necessary
        virtual int transferNext(BufferBase &buffer, int steps);


        SpiMemory_QUADSPI_DMA &device_;

        gpio::Config csPin_;
        qspi::Format format_;
        qspi::CommFormat commFormat_;
        uint8_t readCommand_;
        uint8_t writeEnableCommand_;
        uint8_t writeCommand_;
        uint8_t eraseCommand_;
        uint8_t readStatusCommand_;

        // read status command reads the status into this byte
        volatile uint8_t status_ = false;

        // list of buffers
        IntrusiveList<BufferBase> buffers_;
    };

    /// @brief Buffer for transferring data to/from a SPI slave.
    /// A 4 byte header contains the address in native byte order
    /// @tparam B capacity of buffer
    template <int B>
    class Buffer : public BufferBase {
    public:
        Buffer(Channel &channel) : BufferBase(buffer, B, channel) {}

    protected:
        alignas(4) uint8_t buffer[4 + B];
    };


    /// @brief Call from QUADSPI interrupt handler.
    /// e.g. extern "C" QUADSPI_IRQHandler()
    void QUADSPI_IRQHandler();

protected:
    Loop_Queue &loop_;

    Registers registers_;
    int qspiIrq_;

    // list of active transfers
    InterruptQueue2<BufferBase> transfers_;
};

} // namespace coco
#endif // HAVE_QUADSPI
