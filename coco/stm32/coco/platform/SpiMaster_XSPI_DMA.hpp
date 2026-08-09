#pragma once

#include <coco/align.hpp>
#include <coco/bits.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/InterruptQueue.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/xspi.hpp>
#include <coco/platform/nvic.hpp>


#ifdef HAVE_XSPI
namespace coco {

/// @brief Implementation of SPI master for stm32 with multiple virtual channels using QUADSPI/OCTOSPI.
/// For example for accessing external memory chips.
///
/// Resources:
///   QUADSPI/OCTOSPI
//      SPI master
///   DMA
///     RX channel (read)
///     TX channel (write)
///   GPIO
///     CS-pins
class SpiMaster_XSPI_DMA {
public:
    /// @brief Constructor for the quad SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
    /// @param loop Event loop
    /// @param xspiInfo Info of QUADSPI instance to use
    /// @param pins Pins (SCK, MOSI, MISO or IO0, IO1, ..., IO7, see data sheet)
    /// @param dmaInfo Info of DMA channel to use
    SpiMaster_XSPI_DMA(Loop_Queue &loop, const xspi::Info &xspiInfo, Array<const gpio::Config> pins,
        const dma::Info<> &dmaInfo);


    class Channel;

    // internal buffer base class, derives from IntrusiveListNode for the list of buffers and Loop_Queue::Handler to be notified from the event loop
    class BufferBase : public coco::Buffer, public IntrusiveListNode, public Loop_Queue::CompletionHandler {
        friend class SpiMaster_XSPI_DMA;
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

    protected:
        void onCompletion() override;

        Channel &channel_;
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

    // internal helpers
    using RxChannel = dma::Channel<dma::Mode::RX8>;
    using TxChannel = dma::Channel<dma::Mode::TX8>;
    struct Registers {
        // quad spi
        xspi::Instance xspi;

        // dma channels
        union {
            RxChannel rx;
            TxChannel tx;
        } dma;
    };

    /// @brief Virtual channel to a SPI slave device using a dedicated CS pin.
    ///
    class Channel : public BufferDevice {
        friend class SpiMaster_XSPI_DMA;
        friend class BufferBase;
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), set gpio::Config::INVERT flag for nCS
        Channel(SpiMaster_XSPI_DMA &device, gpio::Config csPin, xspi::Format format);
        ~Channel() override;

        // BufferDevice methods
        int getBufferCount();
        BufferBase &getBuffer(int index);

    protected:
        Registers &registers() {return device_.registers_;}

        // start first transfer
        virtual int transferFirst(BufferBase &buffer) = 0;

        // start next transfer or return false if no more transfers are necessary
        virtual int transferNext(BufferBase &buffer, int steps) = 0;

        SpiMaster_XSPI_DMA &device_;
        gpio::Config csPin_;
        xspi::Format format_;

        // list of buffers
        IntrusiveList<BufferBase> buffers_;
    };

    /// @brief Virtual channel for accessing SPI registers using just one byte header.
    /// The buffer header size must be 4 and contains an uint32_t for the address.
    /// Transfers a byte as header containing a read/write flag and the address.
/*  class ByteChannel : public Channel {
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), set gpio::Config::INVERT flag for nCS
        /// @param format SPI format (prescaler, delay, bank, memory size)
        /// @param addressBits Mask of address bits, typically 0x7f (0x7e for MMA7455L where bit 7 is R/W and bit 0 is don't care)
        /// @param readInstruction Read instruction, typically 0x80 (0x00 for MMA7455L)
        /// @param writeInstruction Write instruction, typically 0x00 (0x80 for for MMA7455L)
        ByteChannel(SpiMaster_XSPI_DMA &device, gpio::Config csPin, xspi::Format format,
            int addressBits = 0x7f,
            int readInstruction = 0x80, int writeInstruction = 0x00)
            : Channel(device, csPin, format)
            , addressMask_(addressBits)
            , addressShift_(firstBit(addressBits))
            , readInstruction_(readInstruction)
            , writeInstruction_(writeInstruction)
        {
        }
        ~ByteChannel() override;

    protected:
        int transferFirst(BufferBase &buffer) override;
        int transferNext(BufferBase &buffer, int steps) override;

        uint8_t addressMask_;
        uint8_t readInstruction_;
        uint8_t writeInstruction_;
        uint8_t header_;
    };*/

    /// @brief Virtual channel for accessing SPI registers.
    /// The buffer header size must be 4 and contains an uint32_t for the register address.
    /// Transfers a header consisting of a command byte and an address of 1 to 4 bytes, followed by data to write or read.
    class RegistersChannel : public Channel {
    public:
        /// @brief Constructor.
        /// @tparam I Type of instruction, e.g. int or enum
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), set gpio::Config::INVERT flag for nCS
        /// @param format SPI format (prescaler, delay, bank, memory size)
        /// @param timing SPI timing (dummy cycles, DDR delay)
        /// @param addressBytes Number of address bytes (1 to 4)
        /// @param readInstruction Read instruction
        /// @param readMode Read mode (e.g. xspi::Mode_1_1_1 or xspi::Mode_1_1_4)
        /// @param readDummyCycles Number of dummy clock cycles after address for read instruction (max. 31)
        /// @param writeInstruction Write instruction
        /// @param writeMode Write mode (e.g. xspi::Mode_1_1_1 or xspi::Mode_1_1_4)
        /// @param writeDummyCycles Number of dummy clock cycles after address for write instruction (max. 31)
        template <typename I>
        RegistersChannel(SpiMaster_XSPI_DMA &device, gpio::Config csPin, xspi::Format format, xspi::Timing timing,
            int addressBytes,
            xspi::Mode readMode, I readInstruction, int readDummyCycles,
            xspi::Mode writeMode, I writeInstruction, int writeDummyCycles)
            : Channel(device, csPin, format)
            , readCommConfig_(xspi::makeCommConfig(readMode, readInstruction, addressBytes, xspi::makeTiming(timing, readDummyCycles)))
            , writeCommConfig_(xspi::makeCommConfig(writeMode, writeInstruction, addressBytes, xspi::makeTiming(timing, writeDummyCycles)))
        {
        }
        ~RegistersChannel() override;

    protected:
        int transferFirst(BufferBase &buffer) override;
        int transferNext(BufferBase &buffer, int steps) override;

        uint32_t readCommConfig_;
        uint32_t writeCommConfig_;
    };

    /// @brief Virtual channel for accessing an SPI memory.
    /// The buffer header size must be 4 and contains an uint32_t for the address.
    /// Transfers a header consisting of a command byte and an address of 1 to 4 bytes, followed by data to write or read.
    class MemoryChannel : public Channel {
    public:
        /// @brief Constructor.
        /// @param device The SPI device to operate on
        /// @param csPin Chip select pin of the slave (CS), set gpio::Config::INVERT flag for nCS
        /// @param format SPI format (prescaler, delay, bank, memory size)
        /// @param timing SPI timing (dummy cycles, DDR delay)
        /// @param addressBytes Number of address bytes (1 to 4)
        /// @param readInstruction Read instruction
        /// @param readMode Read mode (e.g. xspi::Mode_1_1_1 or xspi::Mode_1_1_4)
        /// @param readDummyCycles Number of dummy clock cycles after address for read instruction (max. 31)
        /// @param writeEnableInstruction Write enable instruction
        /// @param writeEnableMode Write enable mode (e.g. xspi::Mode_1_0_0)
        /// @param writeInstruction Write instruction
        /// @param writeMode Write mode (e.g. xspi::Mode_1_1_1 or xspi::Mode_1_1_4)
        /// @param writeDummyCycles Number of dummy clock cycles after address for write instruction (max. 31)
        /// @param eraseInstruction Erase instruction
        /// @param eraseMode Erase mode (e.g. xspi::Mode_1_1_0)
        /// @param readStatusInstruction Read status instruction
        /// @param readStatusMode Read status mode (e.g. xspi::Mode_1_0_1)
        template <typename I>
        MemoryChannel(SpiMaster_XSPI_DMA &device, gpio::Config csPin, xspi::Format format, xspi::Timing timing,
            int addressBytes,
            xspi::Mode readMode, I readInstruction, int readDummyCycles,
            xspi::Mode writeEnableMode, I writeEnableInstruction,
            xspi::Mode writeMode, I writeInstruction, int writeDummyCycles,
            xspi::Mode eraseMode, I eraseInstruction,
            xspi::Mode readStatusMode, I readStatusInstruction)
            : Channel(device, csPin, format)
            , readCommConfig_(xspi::makeCommConfig(readMode, readInstruction, addressBytes, xspi::makeTiming(timing, readDummyCycles)))
            , writeEnableCommConfig_(xspi::makeCommConfig(writeEnableMode, writeEnableInstruction, addressBytes, timing))
            , writeCommConfig_(xspi::makeCommConfig(writeMode, writeInstruction, addressBytes, xspi::makeTiming(timing, writeDummyCycles)))
            , eraseCommConfig_(xspi::makeCommConfig(eraseMode, eraseInstruction, addressBytes, timing))
            , readStatusCommConfig_(xspi::makeCommConfig(readStatusMode, readStatusInstruction, addressBytes, timing))
        {}
        ~MemoryChannel() override;

    protected:
        int transferFirst(BufferBase &buffer) override;
        int transferNext(BufferBase &buffer, int steps) override;

        uint32_t readCommConfig_;
        uint32_t writeEnableCommConfig_;
        uint32_t writeCommConfig_;
        uint32_t eraseCommConfig_;
        uint32_t readStatusCommConfig_;

        // read status command reads the status into this byte
        volatile uint8_t status_ = false;
    };


    /// @brief Call from QUADSPI interrupt handler.
    /// e.g. extern "C" QUADSPI_IRQHandler()
    void XSPI_IRQHandler();

protected:
    Loop_Queue &loop_;

    Registers registers_;
    int xspiIrq_;

    // list of active transfers
    InterruptQueue<BufferBase> transfers_;
};

} // namespace coco
#endif // HAVE_XSPI
