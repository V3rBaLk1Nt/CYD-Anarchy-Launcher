#ifndef ANARCHY_REGISTRY_H
#define ANARCHY_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

enum class AnarchyToolStatus : uint8_t { Working, Partial, HardwareDependent, Research, NotImplemented };
enum class AnarchyToolGroup : uint8_t { Network, Hardware, Files, Codes, System, Research };
enum class AnarchyToolHandler : uint8_t { None, Wifi, Scan, Files, Telemetry, Touch, Settings };

struct AnarchyTool {
    uint8_t id;
    const char *name;
    AnarchyToolGroup group;
    AnarchyToolStatus status;
    AnarchyToolHandler handler;
    const char *scope;
};

size_t anarchyToolCount();
const AnarchyTool *anarchyToolAt(size_t index);
const char *anarchyToolStatusName(AnarchyToolStatus status);
const char *anarchyToolGroupName(AnarchyToolGroup group);
void anarchyToolChest();

#endif
