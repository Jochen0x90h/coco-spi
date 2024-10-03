#pragma once

#include <coco/align.hpp>
#include <coco/BufferDevice.hpp>
#include <coco/platform/Loop_Queue.hpp>
#include <coco/platform/gpio.hpp>
#include <coco/platform/nvic.hpp>
#include <coco/platform/spi.hpp>


namespace coco {

/**
    Implementation of SPI hardware interface for nRF52 with multiple virtual channels.

    Reference manual:
        https://infocenter.nordicsemi.com/topic/ps_nrf52840/spi.html?cp=5_0_0_5_23
    Resources:
        NRF_SPIM3
        GPIO
            CS-pins
*/
class SpiMaster_SPIM3 {
public:
    /**
        Constructor for the SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
        @param loop event loop
        @param sckPin clock pin (SCK)
        @param misoPin master in slave out pin (MISO)
        @param mosiPin master out slave in pin (MOSI)
        @param config configuration such as transfer speed, phase and polarity
    */
    SpiMaster_SPIM3(Loop_Queue &loop, gpio::Config sckPin, gpio::Config misoPin, gpio::Config mosiPin, spi::Config config)
        : SpiMaster_SPIM3(loop, sckPin, misoPin, mosiPin, gpio::Config::NONE, config)
    {}

    /**
        Constructor for the SPI device. For each SPI slave a Channel is needed which drives the CS pin of the slave.
        @param loop event loop
        @param sckPin clock pin (SCK)
        @param misoPin master in slave out pin (MISO)
        @param mosiPin master out slave in pin (MOSI)
        @param dcPin data/command pin (DC) e.g. for displays, can be same as MISO for read-only devices
        @param config configuration such as transfer speed, phase and polarity
    */
    SpiMaster_SPIM3(Loop_Queue &loop,
        gpio::Config sckPin, gpio::Config misoPin, gpio::Config mosiPin, gpio::Config dcPin, spi::Config config);


    class Channel;

    // internal buffer base class, derives from IntrusiveListNode for the list of buffers and Loop_Queue::Handler to be notified from the event loop
    class BufferBase : public coco::Buffer, public IntrusiveListNode, public Loop_Queue::Handler {
        friend class SpiMaster_SPIM3;
    public:
        /**
            Constructor
            @param data data of the buffer
            @param capacity capacity of the buffer
            @param channel channel to attach to
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

        //int headerSize = 0;
        Op op;
    };

    /**
        Virtual channel to a SPI slave device using a dedicated CS pin
    */
    class Channel : public BufferDevice {
        friend class SpiMaster_SPIM3;
        friend class BufferBase;
    public:
        /**
            Constructor
            @param master the SPI master to operate on
            @param csPin chip select pin for the slave (CS), typically nCS, therefore set INVERT flag
            @param dcUsed indicates if DC pin is used and if MISO should be overridden if DC and MISO share the same pin. Maximum size of header supported by hardware for DC pin is 14
        */
        Channel(SpiMaster_SPIM3 &device, gpio::Config csPin, bool dcUsed = false);
        ~Channel();

        // BufferDevice methods
        int getBufferCount() override;
        BufferBase &getBuffer(int index) override;

    protected:
        // list of buffers
        IntrusiveList<BufferBase> buffers;

        SpiMaster_SPIM3 &device;
        gpio::Config csPin;
        bool dcUsed;
    };

    /**
        Buffer for transferring data to/from a SPI slave.
        Note that the header may get overwritten when reading data, therefore always set the header before read() or transfer()
        @tparam C capacity of buffer
    */
    template <int C>
    class Buffer : public BufferBase {
    public:
        Buffer(Channel &channel) : BufferBase(data, C, channel) {}

    protected:
        alignas(4) uint8_t data[C];
    };

    // call from SPI interrupt handler
    void SPIM3_IRQHandler();
protected:

    Loop_Queue &loop;

    // pins
    gpio::Config dcPin;
    bool sharedPin; // set if DC and MISO share the same pin

    // list of active transfers
    InterruptQueue<BufferBase> transfers;
};

} // namespace coco
