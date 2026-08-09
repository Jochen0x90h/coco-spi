#pragma once

#include <coco/align.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/InterruptQueue.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/spi.hpp>
#include <coco/platform/nvic.hpp>


namespace coco {

/// @brief Implementation of SPI master for stm32 with multiple virtual channels using SPI.
///
/// Resources:
///   SPI
//      SPI master
///   DMA
///     RX channel (read)
///     TX channel (write)
///   GPIO
///     CS-pins
class SpiMaster_SPI_DMA {
public:
    /// @brief Constructor for the SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
    /// @param loop Event loop
    /// @param spiInfo Info of SPI instance to use
    /// @param sckPin Clock pin (SCK) and alternate function (see data sheet)
    /// @param mosiPin Master output pin (MOSI) and alternate function (see data sheet), can be NONE
    /// @param misoPin Master input pin (MISO) and alternate function (see data sheet), can be NONE
    /// @param dmaInfo Info of DMA channels to use
    /// @param config SPI configuration
    SpiMaster_SPI_DMA(Loop_Queue &loop, const spi::Info &spiInfo, gpio::Config sckPin, gpio::Config mosiPin,
        gpio::Config misoPin, const dma::DualInfo<> &dmaInfo, spi::Config config = spi::Config::SINGLE_MASTER);


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
        friend class SpiMaster_SPI_DMA;
        friend class BufferBase;
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), set gpio::Config::INVERT flag for nCS
        /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
        /// @param flags Flags, use Flags::VARIABLE_HEADER_SIZE for variable header size
        Channel(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format, Flags flags = Flags::NONE);
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
        Flags flags_;
        volatile uint8_t dummy_;

        // list of buffers
        IntrusiveList<BufferBase> buffers_;
    };

    /// @brief Virtual channel for accessing SPI registers using just one byte header.
    /// The buffer header size must be 4 and contains an uint32_t for the address.
    /// Transfers a byte as header containing a read/write flag and the address.
    class ByteRegistersChannel : public Channel {
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), set gpio::Config::INVERT flag for nCS
        /// @param format SPI format (prescaler, delay, bank, memory size)
        /// @param addressMask Mask of address bits, typically 0x7f (0x7e for MMA7455L where bit 7 is R/W and bit 0 is don't care)
        /// @param readInstruction Read instruction, typically 0x80 (0x00 for MMA7455L)
        /// @param writeInstruction Write instruction, typically 0x00 (0x80 for for MMA7455L)
        ByteRegistersChannel(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format,
            int addressMask = 0x7f,
            int readInstruction = 0x80, int writeInstruction = 0x00)
            : Channel(device, csPin, format)
            , addressMask_(addressMask)
            , readInstruction_(readInstruction)
            , writeInstruction_(writeInstruction)
        {
        }
        ~ByteRegistersChannel() override;

    protected:
        int transferFirst(BufferBase &buffer) override;

        uint8_t addressMask_;
        uint8_t readInstruction_;
        uint8_t writeInstruction_;
        uint8_t header_;
    };

    /// @brief Virtual channel for accessing SPI registers.
    /// The buffer header size must be 4 and contains an uint32_t for the address.
    /// Transfers a header consisting of a command byte and an address of 1 to 4 bytes, followed by data to write or read.
    class RegistersChannel : public Channel {
    public:
        /// @brief Constructor.
        /// @tparam I Type of instruction, e.g. int or enum
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), typically nCS, therefore set gpio::Config::INVERT flag
        /// @param format SPI format (prescaler, phase, polarity, endianness, number of data bits)
        /// @param addressBytes Number of address bytes (1 to 4), or 0 if instruction and address are combined in one byte
        /// @param readInstruction Read instruction
        /// @param readDummyBytes Number of dummy bytes after address for read instruction (addressSize + readDummyBytes <= 6)
        /// @param writeInstruction Write instruction
        /// @param writeDummyBytes Number of dummy bytes after address for write instruction (addressSize + writeDummyBytes <= 6)
        template <typename I>
        RegistersChannel(SpiMaster_SPI_DMA &device, gpio::Config csPin, spi::Format format,
            int addressBytes,
            I readInstruction, int readDummyBytes,
            I writeInstruction, int writeDummyBytes)
            : Channel(device, csPin, format)
            , addressBytes_(addressBytes)
            , readInstruction_(uint8_t(readInstruction)), readDummyBytes_(readDummyBytes)
            , writeInstruction_(uint8_t(writeInstruction)), writeDummyBytes_(writeDummyBytes)
        {
        }
        ~RegistersChannel() override;

    protected:
        // Channel methods
        int transferFirst(SpiMaster_SPI_DMA::BufferBase &buffer) override;

        uint8_t addressBytes_;
        uint8_t readInstruction_;
        uint8_t readDummyBytes_;
        uint8_t writeInstruction_;
        uint8_t writeDummyBytes_;

        uint8_t header_[7] = {}; // command + address (max 4 bytes) + dummy (max 2 bytes)
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
COCO_ENUM(SpiMaster_SPI_DMA::Flags)

} // namespace coco
