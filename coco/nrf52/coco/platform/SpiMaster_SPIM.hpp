#pragma once

#include <coco/align.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/InterruptQueue.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/spim.hpp>
#include <coco/platform/nvic.hpp>


namespace coco {

/// @brief Implementation of SPI hardware interface for nRF52 with multiple virtual channels.
///
/// Resources:
///   NRF_SPIMx
///   GPIO
///     CS-pins
class SpiMaster_SPIM {
public:
    /// @brief Constructor for the SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
    /// @param loop Event loop
    /// @param spiInfo Info of SPI instance to use
    /// @param sckPin Clock pin (SCK)
    /// @param mosiPin Master output pin (MOSI), can be NONE
    /// @param misoPin Master input pin (MISO), can be NONE
    /// @param config SPI configuration
    SpiMaster_SPIM(Loop_Queue &loop, const spim::Info &spiInfo,
        gpio::Config sckPin, gpio::Config mosiPin, gpio::Config misoPin);


    class Channel;

    // internal buffer base class, derives from IntrusiveListNode for the list of buffers and Loop_Queue::Handler to be notified from the event loop
    class BufferBase : public coco::Buffer, public IntrusiveListNode, public Loop_Queue::CompletionHandler {
        friend class SpiMaster_SPIM;
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

    // internal helpers
    struct Registers {
        // spi
        spim::Instance spi;
    };

    enum class Flags {
        NONE = 0,
        SUPPORT_ERASE = 1 << 2,

        // the first byte of the header is the header size, otherwise the header size is fixed
        VARIABLE_HEADER_SIZE = 1 << 3
    };

    /// @brief Virtual channel to a SPI slave device using a dedicated CS pin.
    /// Default implementation transfers header and data as-is.
    /// The header is either fixed or variable size depending on the flags
    class Channel : public BufferDevice {
        friend class SpiMaster_SPIM;
        friend class BufferBase;
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), typically nCS, therefore set gpio::Config::INVERT flag
        /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
        /// @param flags Flags, use Flags::VARIABLE_HEADER_SIZE for variable header size
        Channel(SpiMaster_SPIM &device, gpio::Config csPin, spim::Format format, Flags flags = Flags::NONE);
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
        void start(BufferBase::Op op, volatile void *data, int size) {
            auto &r = registers();
            r.spi.setRxData(data, (op & BufferBase::Op::READ) != 0 ? size : 0);
            r.spi.setTxData(data, (op & BufferBase::Op::WRITE) != 0 ? size : 0);
            r.spi.start();
        }


        SpiMaster_SPIM &device_;
        gpio::Config csPin_;
        spim::Format format_;
        //bool eraseSupported_ = false;
        Flags flags_;
        //volatile uint8_t dummy_;

        // list of buffers
        IntrusiveList<BufferBase> buffers_;
    };


    /// @brief Call from interrupt handler of the SPI master.
    /// See startup_stm32XXX.c, e.g. extern "C" SPIM3_IRQHandler()
    void SPIM_IRQHandler();

protected:
    Loop_Queue &loop_;

    Registers registers_;
    int spiIrq_;

    // list of active transfers
    InterruptQueue<BufferBase> transfers_;
};
COCO_ENUM(SpiMaster_SPIM::Flags)

} // namespace coco
