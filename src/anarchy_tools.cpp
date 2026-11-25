#include "anarchy_registry.h"

#if defined(ANARCHY_BRANDING)
#include "display.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "onlineLauncher.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settings.h"
#include <esp_heap_caps.h>

static void showTelemetry(bool showTouch) {
    resetGlobals();
    const uint8_t savedTextSize = tft->getTextSize();
    tft->fillScreen(BGCOLOR);
    tft->setTextSize(1);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->drawString(showTouch ? "Logical touch diagnostics" : "Hardware telemetry", 8, 8, 1);
    tft->drawString(
        showTouch ? "Raw pressure: serial 115200" : "No external sensor values claimed", 8, 25, 1
    );
    tft->drawRect(0, tftHeight - 25, tftWidth, 25, FGCOLOR);
    tft->drawString("< Back", 8, tftHeight - 20, 1);
    uint32_t lastDraw = 0;
    while (!returnToMenu) {
        if (check(EscPress)) break;
        const LTouchPoint point = touchPoint;
        if (point.pressed && point.y >= tftHeight - 25) break;
        const uint32_t now = launcherMillis();
        if (now - lastDraw >= 150) {
            lastDraw = now;
            char line[80];
            tft->fillRect(8, 48, tftWidth - 16, 72, BGCOLOR);
            if (showTouch) {
                snprintf(line, sizeof(line), "X: %u  Y: %u", point.x, point.y);
                tft->drawString(line, 8, 48, 1);
                tft->drawString(point.pressed ? "State: PRESSED" : "State: RELEASED", 8, 70, 1);
                tft->drawString("Logical input snapshot, not calibration", 8, 92, 1);
            } else {
                snprintf(line, sizeof(line), "Uptime: %lu s", static_cast<unsigned long>(now / 1000));
                tft->drawString(line, 8, 48, 1);
                snprintf(line, sizeof(line), "Free heap: %u bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
                tft->drawString(line, 8, 70, 1);
                snprintf(
                    line, sizeof(line), "Min heap: %u bytes", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)
                );
                tft->drawString(line, 8, 92, 1);
            }
        }
        wakeUpScreen();
        launcherDelayMs(20);
    }
    resetGlobals();
    tft->setTextSize(savedTextSize);
}

static void showWifiScan() {
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
        entries.push_back({name + " " + String(network.rssi) + "dBm", [detail]() {
                               displayMsg(detail, true);
                           }});
    }
    if (entries.empty()) entries.push_back({"No access points found", []() {}});
    entries.push_back({"Back", []() {}});
    loopOptions(entries);
}

static void runTool(const AnarchyTool &tool) {
    launcherConsolePrintf("ANARCHY_TOOL open id=%u status=%s\n", tool.id, anarchyToolStatusName(tool.status));
    switch (tool.handler) {
        case AnarchyToolHandler::Wifi: connectWifi(); break;
        case AnarchyToolHandler::Scan: showWifiScan(); break;
        case AnarchyToolHandler::Files:
            if (setupSdCard()) loopSD(false);
            else displayError("SD card not available");
            break;
        case AnarchyToolHandler::Telemetry: showTelemetry(false); break;
        case AnarchyToolHandler::Touch: showTelemetry(true); break;
        case AnarchyToolHandler::Settings: settings_menu(); break;
        case AnarchyToolHandler::None: break;
    }
}

static void showToolDetails(const AnarchyTool &tool) {
    bool back = false;
    while (!back && !returnToMenu) {
        std::vector<Option> entries = {
            {String(tool.id) + ": " + tool.name,                      []() {}},
            {String("Status: ") + anarchyToolStatusName(tool.status), []() {}},
            {tool.scope,                                              []() {}}
        };
        if (tool.handler != AnarchyToolHandler::None) {
            entries.push_back({"Open implemented scope", [&tool]() { runTool(tool); }});
        }
        entries.push_back({"Back", [&back]() { back = true; }});
        if (loopOptions(entries) < 0) break;
    }
}

static void showToolGroup(AnarchyToolGroup group) {
    bool back = false;
    while (!back && !returnToMenu) {
        std::vector<Option> entries;
        for (size_t index = 0; index < anarchyToolCount(); ++index) {
            const AnarchyTool *tool = anarchyToolAt(index);
            if (tool->group != group) continue;
            const char *marker = tool->handler == AnarchyToolHandler::None ? "[INFO] " : "[OPEN] ";
            entries.push_back({String(marker) + tool->name, [tool]() { showToolDetails(*tool); }});
        }
        entries.push_back({"Back", [&back]() { back = true; }});
        if (loopOptions(entries) < 0) break;
    }
}

void anarchyToolChest() {
    bool back = false;
    returnToMenu = false;
    launcherConsolePrintln("ANARCHY_TOOLS registry=43 tools_not_all_implemented");
    while (!back && !returnToMenu) {
        std::vector<Option> entries;
        for (AnarchyToolGroup group :
             {AnarchyToolGroup::Network,
              AnarchyToolGroup::Hardware,
              AnarchyToolGroup::Files,
              AnarchyToolGroup::Codes,
              AnarchyToolGroup::System,
              AnarchyToolGroup::Research}) {
            entries.push_back({anarchyToolGroupName(group), [group]() { showToolGroup(group); }});
        }
        entries.push_back({"Back to Launcher", [&back]() { back = true; }});
        if (loopOptions(entries) < 0) break;
    }
}
#endif
