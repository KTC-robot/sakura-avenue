#include "sensor_control.hpp"


SensorState judgeDistance(uint16_t distance) {
    if (distance <= 100) {
        return STOP;
    } else if (distance <= 200) {
        return SLOW;
    } else {
        return NORMAL;
    }
}