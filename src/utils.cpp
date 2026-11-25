#include "utils.h"
#include "display.h"
#include "esp_wifi.h"
#include "settings.h"
#include <SD.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <globals.h>
/*********************************************************************
** Function: touchHeatMap
** Touchscreen Mapping, include this function after reading the touchPoint
**********************************************************************/
void touchHeatMap(struct LTouchPoint t) {
#if 1
#if !defined(HAS_TOUCH_NO_BORDER)
    // Ⓐ🧩 WHY: the old zones reached two text rows ABOVE the footer, so taps
    // meant to scroll also fired the highlighted row (accidental launches).
    // Ⓐ🔗 WHERE: zones live strictly below tftHeight (44 px nav strip), split
    // into five columns matching TouchFooter()'s buttons: HOME UP OK DN BACK.
    const char *zone = "none";
    if (t.y > tftHeight) {
        const int col = t.x / (tftWidth / 5);
        if (col <= 1)
            PrevPress = true; // UP
        else if (col == 2)
            SelPress = true; // OK
        else if (col == 3)
            NextPress = true; // DN
        else
            EscPress = true; // BACK
        if (col == 0) EscPress = true; // HOME behaves as BACK: up one level
        switch (col) {
            case 0: zone = "HOME"; break;
            case 1: zone = "UP"; break;
            case 2: zone = "OK"; break;
            case 3: zone = "DN"; break;
            default: zone = "BACK"; break;
        }
    } else if (t.x > (tftWidth - LW * _fm * 6) && t.y < LH * _fm * 3) {
        EscPress = true;
        zone = "ESC-corner";
    } else {
        zone = "content-row";
    }
#if defined(ANARCHY_TOUCH_TELEMETRY)
    // 0x11/0x12: raw XYZ come from the CYD driver log; this is the mapped
    // point plus which hitbox it landed in, so serial shows both layers.
    // 0x15: bounds -- the mapped point must stay on the physical panel.
    if (t.x < 0 || t.x >= tftWidth || t.y < 0 || t.y >= tftHeight + ANARCHY_NAV_H) {
        Serial.printf(
            "ANARCHY_ZONE BOUNDS_VIOLATION x=%d y=%d w=%d h=%d\n",
            t.x,
            t.y,
            tftWidth,
            tftHeight + ANARCHY_NAV_H
        );
    } else {
        Serial.printf("ANARCHY_ZONE x=%d y=%d zone=%s\n", t.x, t.y, zone);
    }
#endif

    /*
                        Touch area Map (landscape 320x240, nav strip 44 px)
                ________________________________ 0
                |______________________|_Esc__| Top Right corner, 3 lines height, 6 characters width
                |______________________|_Esc__|
                |_______________________________|
                |        list rows here        |
                |_______________________________|
                |__HOME___|__UP__|_OK_|__DN__|__BACK__|  tftHeight .. 240: nav strip
                0                                 tftWidth
    */
#endif

#else
    int third_x = tftWidth / 3;
    int third_y = tftHeight / 3;
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y > third_y) PrevPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && ((t.y > third_y && t.y < third_y * 2) || t.y > tftHeight))
        SelPress = true;
    if (t.x > third_x * 2 && t.x < third_x * 3) NextPress = true;
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y < third_y) EscPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y < third_y) UpPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y > third_y * 2 && t.y < third_y * 3) DownPress = true;
#endif
}

// Prefer PSRAM (isolates ArduinoJson churn from the internal heap), fall back
// to internal RAM on boards without PSRAM (e.g. m5stack-cardputer).
class LauncherJsonAllocator : public ArduinoJson::Allocator {
public:
    void *allocate(size_t size) override {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }

    void deallocate(void *ptr) override { heap_caps_free(ptr); }

    void *reallocate(void *ptr, size_t new_size) override {
        // heap_caps_realloc leaves the original block untouched when it returns
        // NULL, so retrying with a different capability is safe.
        void *p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }
};

ArduinoJson::Allocator *launcherJsonAllocator() {
    static LauncherJsonAllocator instance;
    return &instance;
}

void buildFirmwareListFilter(JsonDocument &filter) {
    filter["total"] = true;
    filter["page"] = true;
    filter["page_size"] = true;
    // The first element of a filter array is the template applied to every
    // element of the input array.
    JsonObject item = filter["items"].add<JsonObject>();
    item["fid"] = true;
    item["file"] = true;
    item["name"] = true;
    item["version"] = true;
    item["author"] = true;
    item["star"] = true;
}

bool releaseHeapObjectsAndReboot(void) {
    static volatile bool rebootInProgress = false;
    if (rebootInProgress) {
        reboot();
        ESP.restart(); // just in case
        return true;
    }
    rebootInProgress = true;

    if (xHandle != nullptr) vTaskSuspend(xHandle);
    esp_wifi_stop();

    doc.clear();
    doc.shrinkToFit();

    JsonArray favorites = settings["favorite"].as<JsonArray>();
    if (!favorites.isNull()) favorites.clear();
    favorite = JsonArray();

    settings.clear();
    settings.shrinkToFit();

    SDM.end();
    options.clear();

    fileToCopy = "";
    ssid = "";
    pwd = "";
    wui_usr = "";
    wui_pwd = "";
    dwn_path = "";
    lastInstalledApp = "";
    tft->fillScreen(NATIVE_BGCOLOR);
    tft->display(true);
    FREE_TFT
    reboot();
    ESP.restart(); // just in case
    return true;
}
