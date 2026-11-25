#include "anarchy_apps.h"

#if defined(ANARCHY_BRANDING)

#include "display.h"
#include "globals.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "onlineLauncher.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settings.h"
#include <NimBLEDevice.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <set>

enum class AnarchyAppGroup : uint8_t { Ir, Rf, Wifi, Ble, NfcRfid, System };
enum class AnarchyAppMode : uint8_t { Real, Gated };

struct AnarchyApp {
    uint8_t id;
    const char *name;
    AnarchyAppGroup group;
    AnarchyAppMode mode;
    const char *module;
    const char *scope;
};

static const AnarchyApp apps[] = {
    {21,  "IR TX",       AnarchyAppGroup::Ir,   AnarchyAppMode::Gated, "IR LED",              "Needs external IR LED module"},
    {22,  "IR RX",       AnarchyAppGroup::Ir,   AnarchyAppMode::Gated, "IR receiver",         "Needs external IR receiver"},
    {23,  "IR Remote",   AnarchyAppGroup::Ir,   AnarchyAppMode::Gated, "IR LED + receiver",   "Needs external IR transceiver"},
    {24,  "IR Learn",    AnarchyAppGroup::Ir,   AnarchyAppMode::Gated, "IR receiver",         "Needs external IR receiver"},
    {25,  "RF Scan",     AnarchyAppGroup::Rf,   AnarchyAppMode::Gated, "CC1101/NRF24",        "Needs external RF module"},
    {26,  "RF RX",       AnarchyAppGroup::Rf,   AnarchyAppMode::Gated, "CC1101/NRF24",        "Needs external RF module"},
    {27,  "RF TX",       AnarchyAppGroup::Rf,   AnarchyAppMode::Gated, "CC1101/NRF24",        "Needs external RF module"},
    {28,  "RF Record",   AnarchyAppGroup::Rf,   AnarchyAppMode::Gated, "CC1101/NRF24",        "Needs external RF module"},
    {29,  "RF Replay",   AnarchyAppGroup::Rf,   AnarchyAppMode::Gated, "CC1101/NRF24",        "Needs external RF module"},
    {30,  "RF Analyzer", AnarchyAppGroup::Rf,   AnarchyAppMode::Gated, "CC1101/NRF24",        "Needs external RF module"},
    {31,  "Wi-Fi Scan",  AnarchyAppGroup::Wifi, AnarchyAppMode::Real,  "",                    "Onboard scan; RSSI and auth"},
    {32,  "Wi-Fi Analyze", AnarchyAppGroup::Wifi, AnarchyAppMode::Real, "",                    "Multi-scan stats; scan-based only"},
    {33,  "Wi-Fi Monitor", AnarchyAppGroup::Wifi, AnarchyAppMode::Real, "",                    "Rescan loop; not packet capture"},
    {34,  "Wi-Fi Info",  AnarchyAppGroup::Wifi, AnarchyAppMode::Real,  "",                     "MAC, IP and link status"},
    {35,  "BLE Scan",    AnarchyAppGroup::Ble,  AnarchyAppMode::Real,  "",                     "Onboard BLE advertisements"},
    {36,  "BLE Explorer", AnarchyAppGroup::Ble, AnarchyAppMode::Real, "",                     "Advertisement details"},
    {37,  "BLE Monitor", AnarchyAppGroup::Ble,  AnarchyAppMode::Real,  "",                     "Repeated scans, unique count"},
    {38,  "NFC Read",    AnarchyAppGroup::NfcRfid, AnarchyAppMode::Gated, "PN532",             "Needs external NFC module"},
    {39,  "NFC Write",   AnarchyAppGroup::NfcRfid, AnarchyAppMode::Gated, "PN532",             "Needs external NFC module"},
    {40,  "RFID Read",   AnarchyAppGroup::NfcRfid, AnarchyAppMode::Gated, "RC522",             "Needs external RFID module"},
    {41,  "RFID Write",  AnarchyAppGroup::NfcRfid, AnarchyAppMode::Gated, "RC522",             "Needs external RFID module"},
    {42,  "GPIO Tool",   AnarchyAppGroup::System, AnarchyAppMode::Real, "",                    "Safe pins only; readback verified"},
    {43,  "I2C Scanner", AnarchyAppGroup::System, AnarchyAppMode::Real, "",                    "Bus probe 0x01-0x7F"},
    {44,  "File Manager", AnarchyAppGroup::System, AnarchyAppMode::Real, "SD card",            "SD browser when card mounted"},
    {45,  "Settings",    AnarchyAppGroup::System, AnarchyAppMode::Real, "",                    "Existing settings UI/NVS"},
};

static size_t appCount() { return sizeof(apps) / sizeof(apps[0]); }


static void runGated(const AnarchyApp &app) {
    String msg = String(app.name) + ": no " + app.module + " hardware on this board. Factory schematic WZ2432R024 has no IR/RF/NFC/RFID parts. Needs external " + app.module + " module.";
    displayMsg(msg, true);
}

// Ⓐ🧩 WHY: custom app screens drew a thin 25 px "< Back" strip; small targets
// are hard to hit on a resistive panel and every screen needs an obvious exit.
// Ⓐ🔗 WHERE: one shared large 32 px back bar + tap test used by all screens.
static constexpr int16_t kBackBarH = 32;

static void drawBackBar(const char *label) {
    tft->fillRect(0, tftHeight - kBackBarH, tftWidth, kBackBarH, BGCOLOR);
    tft->drawRect(0, tftHeight - kBackBarH, tftWidth, kBackBarH, FGCOLOR);
    tft->setTextSize(2);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->drawCentreString(label, tftWidth / 2, tftHeight - kBackBarH + 9, 1);
    tft->setTextSize(1);
}

static bool backBarTapped() {
    if (touchPoint.pressed && touchPoint.y >= tftHeight - kBackBarH) {
        touchPoint.pressed = false;
        return true;
    }
    return false;
}

static void wifiScanApp() {
    displayRedStripe("Scanning APs...");
    std::vector<LauncherWifiAp> networks;
    if (launcherWifiScan(networks) < 0) {
        displayError("Wi-Fi scan failed");
        return;
    }
    std::vector<Option> entries;
    for (const LauncherWifiAp &network : networks) {
        const String name = network.ssid.empty() ? "<hidden>" : network.ssid.c_str();
        const String detail = "RSSI " + String(network.rssi) + " dBm; auth " + String(network.authmode);
        entries.push_back({name + " " + String(network.rssi) + "dBm", [detail]() { displayMsg(detail, true); }});
    }
    if (entries.empty()) entries.push_back({"No access points found", []() {}});
    entries.push_back({"Back", []() {}});
    loopOptions(entries);
}

static void wifiAnalyzeApp() {
    displayRedStripe("Analyzing (3 scans)...");
    std::set<std::string> unique;
    int total = 0, openNets = 0, best = -128, worst = 0, bins[7] = {0};
    for (int scan = 0; scan < 3; ++scan) {
        std::vector<LauncherWifiAp> networks;
        if (launcherWifiScan(networks) < 0) {
            displayError("Wi-Fi scan failed");
            return;
        }
        for (const LauncherWifiAp &network : networks) {
            ++total;
            unique.insert(network.ssid);
            if (network.authmode == 0) ++openNets;
            if (network.rssi > best) best = network.rssi;
            if (network.rssi < worst) worst = network.rssi;
            int bin = (-network.rssi - 30) / 10;
            if (bin < 0) bin = 0;
            if (bin > 6) bin = 6;
            ++bins[bin];
        }
    }
    tft->fillScreen(BGCOLOR);
    tft->setTextSize(1);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->drawString("Wi-Fi Analyzer (scan-based)", 8, 8, 1);
    tft->drawString("Beacons seen: " + String(total), 8, 26, 1);
    tft->drawString("Unique SSIDs: " + String(unique.size()), 8, 38, 1);
    tft->drawString("Open: " + String(openNets) + "  Secured: " + String(total - openNets), 8, 50, 1);
    tft->drawString("Best: " + String(best) + " dBm  Worst: " + String(worst) + " dBm", 8, 62, 1);
    tft->drawString("RSSI histogram (10dB bins)", 8, 80, 1);
    int maxBin = 1;
    for (int i = 0; i < 7; ++i)
        if (bins[i] > maxBin) maxBin = bins[i];
    const int barW = (tftWidth - 16) / 7;
    for (int i = 0; i < 7; ++i) {
        const int h = (bins[i] * 50) / maxBin;
        const int x = 8 + i * barW;
        tft->fillRect(x, 140 - h, barW - 2, h, FGCOLOR);
        tft->drawString(String(bins[i]), x, 142, 1);
    }
    tft->drawString("Not packet capture; AP scan stats only", 8, tftHeight - kBackBarH - 12, 1);
    drawBackBar("< Back");
    while (!returnToMenu) {
        if (check(EscPress)) break;
        if (backBarTapped()) break;
        wakeUpScreen();
        launcherDelayMs(20);
    }
}

static void wifiMonitorApp() {
    uint32_t scanNo = 0;
    while (!returnToMenu) {
        if (check(EscPress)) break;
        std::vector<LauncherWifiAp> networks;
        if (launcherWifiScan(networks) < 0) {
            displayError("Wi-Fi scan failed");
            return;
        }
        ++scanNo;
        tft->fillScreen(BGCOLOR);
        tft->setTextSize(1);
        tft->setTextColor(FGCOLOR, BGCOLOR);
        tft->drawString("Wi-Fi Monitor #" + String(scanNo), 8, 8, 1);
        tft->drawString("APs: " + String(networks.size()), 8, 26, 1);
        int shown = 0;
        for (const LauncherWifiAp &network : networks) {
            if (shown >= 8) break;
            String name = network.ssid.empty() ? "<hidden>" : network.ssid.c_str();
            if (name.length() > 22) name = name.substring(0, 22);
            tft->drawString(name + " " + String(network.rssi), 8, 44 + shown * 14, 1);
            ++shown;
        }
        drawBackBar("< Back (rescans ~4s)");
        if (backBarTapped()) break;
        uint32_t start = launcherMillis();
        while (launcherMillis() - start < 4000) {
            if (check(EscPress)) return;
            if (backBarTapped()) return;
            wakeUpScreen();
            launcherDelayMs(50);
        }
    }
}

static void wifiInfoApp() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    tft->fillScreen(BGCOLOR);
    tft->setTextSize(1);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->drawString("Wi-Fi Info", 8, 8, 1);
    tft->drawString("STA MAC: " + String(macStr), 8, 30, 1);
    if (launcherWifiIsConnected()) {
        tft->drawString("Link: CONNECTED", 8, 48, 1);
        tft->drawString("IP: " + String(launcherWifiLocalIp().c_str()), 8, 62, 1);
    } else {
        tft->drawString("Link: not connected", 8, 48, 1);
        tft->drawString("Use Wi-Fi manager to associate", 8, 62, 1);
    }
    tft->drawString("Free heap: " + String(ESP.getFreeHeap()) + " B", 8, 80, 1);
    drawBackBar("< Back");
    while (!returnToMenu) {
        if (check(EscPress)) break;
        if (backBarTapped()) break;
        wakeUpScreen();
        launcherDelayMs(20);
    }
}

static bool bleInitDone = false;

static bool ensureBle() {
    if (!bleInitDone) {
        NimBLEDevice::init("");
        bleInitDone = true;
    }
    return true;
}

static void bleScanApp(bool explorer) {
    ensureBle();
    displayRedStripe("BLE scanning 4s...");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->getResults(4000, false);
    std::vector<Option> entries;
    for (auto it = results.begin(); it != results.end(); ++it) {
        const NimBLEAdvertisedDevice *device = *it;
        const String label = String(device->getName().c_str()) + " " + String(device->getRSSI());
        if (explorer) {
            const String name = device->getName().c_str();
            const String addr = device->getAddress().toString().c_str();
            const int rssi = device->getRSSI();
            const uint8_t addrType = device->getAddressType();
            const uint16_t appearance = device->getAppearance();
            const uint16_t advLen = device->getAdvLength();
            const String detail2 = String("Name: ") + (name.length() ? name : String("<none>")) +
                                  "\nAddr: " + addr + "\nType: " + String(addrType) + "  RSSI: " + String(rssi) +
                                  "\nAppearance: " + String(appearance) + "\nAdv len: " + String(advLen);
            entries.push_back({label, [detail2]() { displayMsg(detail2, true); }});
        } else {
            entries.push_back({label, []() {}});
        }
    }
    if (entries.empty()) entries.push_back({"No BLE advertisements found", []() {}});
    entries.push_back({"Back", []() {}});
    loopOptions(entries);
    scan->clearResults();
}

static void bleMonitorApp() {
    ensureBle();
    std::set<std::string> seen;
    uint32_t scanNo = 0;
    while (!returnToMenu) {
        if (check(EscPress)) break;
        NimBLEScan *scan = NimBLEDevice::getScan();
        scan->setActiveScan(false);
        NimBLEScanResults results = scan->getResults(3000, false);
        for (auto it = results.begin(); it != results.end(); ++it)
            seen.insert((*it)->getAddress().toString());
        ++scanNo;
        tft->fillScreen(BGCOLOR);
        tft->setTextSize(1);
        tft->setTextColor(FGCOLOR, BGCOLOR);
        tft->drawString("BLE Monitor #" + String(scanNo), 8, 8, 1);
        tft->drawString("Unique devices: " + String(seen.size()), 8, 26, 1);
        tft->drawString("Passive rescan every ~3s", 8, 44, 1);
        drawBackBar("< Back");
        scan->clearResults();
        uint32_t start = launcherMillis();
        while (launcherMillis() - start < 1000) {
            if (check(EscPress)) return;
            if (backBarTapped()) return;
            wakeUpScreen();
            launcherDelayMs(50);
        }
    }
}

static void gpioToolApp() {
    static const uint8_t outPins[] = {21, 22, 25, 26, 32};
    static const uint8_t inPins[] = {34, 35, 36, 39};
    std::vector<Option> entries;
    for (uint8_t pin : outPins)
        entries.push_back({"GPIO" + String(pin) + " write test", [pin]() {
                               pinMode(pin, OUTPUT);
                               String report = "GPIO" + String(pin);
                               digitalWrite(pin, LOW);
                               launcherDelayMs(50);
                               report += " LOW read: " + String(digitalRead(pin));
                               digitalWrite(pin, HIGH);
                               launcherDelayMs(50);
                               report += "\nHIGH read: " + String(digitalRead(pin));
                               if (pin == 25 || pin == 26) report += "\nMay drive onboard amp";
                               displayMsg(report, true);
                           }});
    for (uint8_t pin : inPins)
        entries.push_back({"GPIO" + String(pin) + " read only", [pin]() {
                               pinMode(pin, INPUT);
                               displayMsg("GPIO" + String(pin) + " level: " + String(digitalRead(pin)), true);
                           }});
    entries.push_back({"Protected: display 14/13/4/15/2/27", []() {}});
    entries.push_back({"Protected: touch 33, strapping 0/2/12, flash 6-11", []() {}});
    entries.push_back({"Protected: SD 18/19/23/5, UART 1/3", []() {}});
    entries.push_back({"Back", []() {}});
    loopOptions(entries);
}

static void i2cScannerApp() {
    displayRedStripe("I2C scan SDA21 SCL22...");
    Wire.begin(21, 22);
    std::vector<Option> entries;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            entries.push_back({"Found 0x" + String(addr, HEX), []() {}});
        }
    }
    Wire.end();
    if (entries.empty()) entries.push_back({"No I2C devices found", []() {}});
    entries.push_back({"Back", []() {}});
    loopOptions(entries);
}

static void fileManagerApp() {
    if (setupSdCard()) {
        loopSD(false);
    } else {
        displayError("SD card not detected on this board", true);
    }
}

static void runApp(const AnarchyApp &app) {
    launcherConsolePrintf(
        "ANARCHY_APP open id=%u name=%s mode=%s\n",
        app.id,
        app.name,
        app.mode == AnarchyAppMode::Real ? "real" : "gated"
    );
    if (app.mode == AnarchyAppMode::Gated) {
        runGated(app);
        return;
    }
    switch (app.id) {
        case 31: wifiScanApp(); break;
        case 32: wifiAnalyzeApp(); break;
        case 33: wifiMonitorApp(); break;
        case 34: wifiInfoApp(); break;
        case 35: bleScanApp(false); break;
        case 36: bleScanApp(true); break;
        case 37: bleMonitorApp(); break;
        case 42: gpioToolApp(); break;
        case 43: i2cScannerApp(); break;
        case 44: fileManagerApp(); break;
        case 45: settings_menu(); break;
    }
}

static void showAppGroups(std::initializer_list<AnarchyAppGroup> groups) {
    bool back = false;
    while (!back && !returnToMenu) {
        std::vector<Option> entries;
        for (size_t index = 0; index < appCount(); ++index) {
            const AnarchyApp *app = &apps[index];
            bool inGroup = false;
            for (AnarchyAppGroup g : groups) {
                if (app->group == g) inGroup = true;
            }
            if (!inGroup) continue;
            // Settings has its own top-level category; skip it inside System Tools.
            if (app->id == 45) continue;
            String label = app->name;
            if (app->mode == AnarchyAppMode::Gated) label += String(" (") + app->module + ")";
            entries.push_back({label, [app]() { runApp(*app); }});
        }
        entries.push_back({"Back", [&back]() { back = true; }});
        if (loopOptions(entries) < 0) break;
    }
}

// Ⓐ🧩 WHY: the primary list mixed the 11 onboard apps with 14 module-gated
// entries and tagged every row with [GATE]/[OPEN] prefixes -- confusing labels
// in the main menu.
// Ⓐ🔗 WHERE: primary list = six categories; external-hardware tools (RF
// Spectrum, IR/NFC/RFID) are labeled "(external)" in the category row, and
// each gated tool states the module it needs inside its own row.
void anarchyAppsMenu() {
    bool back = false;
    returnToMenu = false;
    launcherConsolePrintln("ANARCHY_APPS registry=25 real=11 gated=14 menu=categories");
    while (!back && !returnToMenu) {
        std::vector<Option> entries;
        entries.push_back({"RF Spectrum (external)", []() {
                               launcherConsolePrintln("ANARCHY_APPS menu=rf-spectrum count=6");
                               showAppGroups({AnarchyAppGroup::Rf});
                           }});
        entries.push_back({"WiFi", []() { showAppGroups({AnarchyAppGroup::Wifi}); }});
        entries.push_back({"Bluetooth", []() { showAppGroups({AnarchyAppGroup::Ble}); }});
        entries.push_back({"IR / NFC / RFID (external)", []() {
                               launcherConsolePrintln("ANARCHY_APPS menu=ir-nfc-rfid count=8");
                               showAppGroups({AnarchyAppGroup::Ir, AnarchyAppGroup::NfcRfid});
                           }});
        entries.push_back({"System Tools", []() { showAppGroups({AnarchyAppGroup::System}); }});
        entries.push_back({"Settings", []() { settings_menu(); }});
        entries.push_back({"Back", [&back]() { back = true; }});
        if (loopOptions(entries) < 0) break;
    }
}
#endif
