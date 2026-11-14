#include "esp_log.h"
#include "control.hpp"
#include "protocol.hpp"
#include "c620.hpp"
extern "C" void app_main() {
    // Intentionally no network/CAN initialization until hardware and authentication
    // are reviewed. There is no compile-time switch that silently enables motors.
    sakura::Safety safety;
    ESP_LOGW("sakura", "DISARM: geometry/GPIO/network/interlock unconfigured; outputs disabled");
}
