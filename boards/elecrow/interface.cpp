#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "powerSave.h"
#include <SPI.h>
#include <Wire.h>
#include <interface.h>

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    // rotation:        0      1      2      3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {true, false, false, true};
    bool mirrorY[4] = {false, false, true, true};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

void _setup_gpio() {
    // Manufacturer wiring: XPT2046 chip select is GPIO33 (schematic + factory demo).
    launcherGpioOutput(33);
    launcherGpioWrite(33, HIGH);
}

void _post_setup_gpio() {
    // Ⓐ🧩 WHY: touch claims the display's SCK/MOSI; starting it before the boot
    // animation kills the screen. Init deferred to anarchyDeferredTouchInit().
    // Brightness control must be initialized after tft in this case @Pirata
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void anarchyDeferredTouchInit() {
    // The display's Arduino_HWSPI backend began the global SPI bus with
    // begin(SCLK=14, MISO=4, MOSI=13), so the touch controller can share that
    // exact hardware bus.
    Serial.println("DBG deferred touch init enter");
    const bool touchStarted = hal_touch_init(touchCfg(), 0x5D, false);
    Serial.printf("DBG touchStarted=%d bus=%p\n", (int)touchStarted, (void *)SPI.bus());
    // Raw SPI liveness read on the shared bus.
    SPI.beginTransaction(SPISettings(600000, MSBFIRST, SPI_MODE0));
    digitalWrite(33, LOW);
    SPI.transfer(0xB1);
    uint16_t v = SPI.transfer16(0);
    digitalWrite(33, HIGH);
    SPI.endTransaction();
    Serial.printf("DBG raw16=%04X\n", v);
    // Raw bit-bang probe on MISO (GPIO 4) independent of the driver.
    pinMode(4, INPUT);
    int rb0 = digitalRead(4);
    delay(5);
    int rb1 = digitalRead(4);
    Serial.printf("DBG raw_miso_gpio4=%d %d\n", rb0, rb1);

    // Pad-follow probes: can we drive/read CLK/MOSI/CS at the pad?
    // Use INPUT_OUTPUT so gpio_get_level reflects pad while we drive.
    auto padFollow = [](int pin, const char *name) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << pin;
        io.mode = GPIO_MODE_INPUT_OUTPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);
        digitalWrite(pin, HIGH);
        delayMicroseconds(2);
        int rh = gpio_get_level((gpio_num_t)pin);
        digitalWrite(pin, LOW);
        delayMicroseconds(2);
        int rl = gpio_get_level((gpio_num_t)pin);
        Serial.printf("DBG pad %s pin=%d wr1->%d wr0->%d\n", name, pin, rh, rl);
    };
    // Detach matrix like takeover does, then test pad-follow.
    pinMatrixOutDetach(14, false, false);
    pinMatrixOutDetach(13, false, false);
    padFollow(14, "CLK");
    padFollow(13, "MOSI");
    padFollow(33, "CS");

    // MISO pullup: floating vs held-low.
    pinMode(4, INPUT_PULLUP);
    delay(2);
    int pu0 = digitalRead(4);
    delay(5);
    int pu1 = digitalRead(4);
    Serial.printf("DBG miso_pullup=%d %d\n", pu0, pu1);

    // Manual bit-bang XPT2046 read (CS low, 0xB1/0xC1/0x91), count MISO 1-bits.
    pinMode(4, INPUT);
    pinMode(33, OUTPUT);
    digitalWrite(33, HIGH);
    digitalWrite(14, LOW);
    digitalWrite(13, LOW);
    delayMicroseconds(5);
    digitalWrite(33, LOW);
    delayMicroseconds(10);
    auto bbXfer = [](uint8_t val) -> uint8_t {
        uint8_t out = 0;
        for (int bit = 7; bit >= 0; --bit) {
            digitalWrite(13, (val & (1 << bit)) ? HIGH : LOW);
            delayMicroseconds(2);
            digitalWrite(14, HIGH);
            delayMicroseconds(2);
            int b = digitalRead(4);
            out = (out << 1) | (b ? 1 : 0);
            digitalWrite(14, LOW);
            delayMicroseconds(2);
        }
        return out;
    };
    auto bbXfer16 = [&](uint16_t data) -> uint16_t {
        uint8_t msb = bbXfer((data >> 8) & 0xFF);
        uint8_t lsb = bbXfer(data & 0xFF);
        return ((uint16_t)msb << 8) | lsb;
    };
    bbXfer(0xB1);
    uint16_t mz1 = bbXfer16(0xC1) >> 3;
    uint16_t mz2 = bbXfer16(0x91) >> 3;
    int mz = (int)mz1 + 4095 - (int)mz2;
    // sample MISO while toggling CLK with CS low (count 1-bits over 64 clocks)
    int ones = 0;
    for (int i = 0; i < 64; ++i) {
        digitalWrite(14, HIGH);
        delayMicroseconds(2);
        if (digitalRead(4)) ++ones;
        digitalWrite(14, LOW);
        delayMicroseconds(2);
    }
    digitalWrite(33, HIGH);
    Serial.printf("DBG manual z1=%u z2=%u z=%d miso1bits=%d\n", mz1, mz2, mz, ones);

    // Restore shared SPI for display after our manual detach/probe.
#if defined(ESP32) && defined(CYD28_TouchR_SHARED_SPI_SCLK) && defined(CYD28_TouchR_SHARED_SPI_MOSI)
    {
        spi_t *bus = SPI.bus();
        if (bus) {
            spiAttachSCK(bus, CYD28_TouchR_SHARED_SPI_SCLK);
            spiAttachMOSI(bus, CYD28_TouchR_SHARED_SPI_MOSI);
            Serial.printf("DBG restored sck=%d mosi=%d bus=%p\n", CYD28_TouchR_SHARED_SPI_SCLK, CYD28_TouchR_SHARED_SPI_MOSI, (void *)bus);
        } else {
            Serial.println("DBG restore bus null");
        }
    }
#endif
    Serial.println("DBG diagdone");
    if (!touchStarted) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started (deferred)").c_str());
#if defined(ANARCHY_TOUCH_TELEMETRY)
    // 0x13/0x14/0x16: rotation mapping, calibration source, and the exact
    // hitbox geometry for this boot -- one line, compared against taps.
    {
        DeviceTouch cfg = touchCfg();
        const uint8_t r = rotation & 0x03;
        Serial.printf(
            "ANARCHY_TOUCH_INFO rotation=%d w=%d h=%d nav=%d swapXY=%d mirrorX=%d mirrorY=%d\n",
            rotation,
            tftWidth,
            tftHeight,
            ANARCHY_NAV_H,
            (int)cfg.SwapXY[r],
            (int)cfg.MirrorX[r],
            (int)cfg.MirrorY[r]
        );
        Serial.printf(
            "ANARCHY_TOUCH_ZONES colw=%d navTop=%d home=[0..%d) up=[%d..%d) ok=[%d..%d) dn=[%d..%d) back=[%d..%d]\n",
            tftWidth / 5,
            tftHeight,
            tftWidth / 5,
            tftWidth / 5,
            2 * (tftWidth / 5),
            2 * (tftWidth / 5),
            3 * (tftWidth / 5),
            3 * (tftWidth / 5),
            4 * (tftWidth / 5),
            4 * (tftWidth / 5),
            tftWidth
        );
    }
#endif
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    static unsigned long tm = 0;
    // Ⓐ🧩 WHY: a held finger re-armed touchPoint every 200 ms poll, so one
    // physical tap selected a row, then fired it again, then leaked into the
    // parent menu -- duplicate taps and accidental double-activation.
    // Ⓐ🔗 WHERE: one press = exactly one event; the next event requires a
    // release (a poll with no touch) in between. The driver's z-threshold
    // (pressure filter) and besttwoavg median already reject noise samples.
    static bool held = false;
    if (launcherMillis() - tm > 200 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
#ifdef ANARCHY_TOUCH_TELEMETRY
            if (!held) Serial.printf("ANARCHY_APPLY x=%d y=%d long=%u\n", t.x, t.y, LongPress ? 1 : 0);
#endif
#ifdef ANARCHY_TOUCH_DIAGNOSTICS
            // Observe raw/scaled readings without allowing phantom presses to
            // drive the Launcher menus during board bring-up.
            touchPoint.pressed = false;
            return;
#endif
            if (!held) {
                held = true;
                if (!hal_touch_apply(t)) return;
            }
        } else {
            // 0x18: release verification -- a poll with no finger must clear
            // the press; the edge-triggered 'held' gate needs this to re-arm.
            if (held) {
#ifdef ANARCHY_TOUCH_TELEMETRY
                Serial.println("ANARCHY_RELEASE");
#endif
                held = false;
            }
            touchPoint.pressed = false;
        }
    }
}
