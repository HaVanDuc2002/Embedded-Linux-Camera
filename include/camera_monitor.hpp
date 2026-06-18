#ifndef CAMERA_MONITOR_HPP
#define CAMERA_MONITOR_HPP

#include "camera_monitor_uapi.h"

#include <cstdint>

namespace streamer {

class CameraMonitor {
public:
    explicit CameraMonitor(const char* device = CAMERA_MONITOR_DEVICE_PATH);
    ~CameraMonitor();

    CameraMonitor(const CameraMonitor&) = delete;
    CameraMonitor& operator=(const CameraMonitor&) = delete;

    bool isAvailable() const { return fd_ >= 0; }
    bool resetCounters();
    bool getStats(camera_monitor_stats& stats);
    bool setState(camera_monitor_state state);
    bool reportFrame(uint64_t count = 1);
    bool reportDrop(uint64_t count = 1);
    bool reportError(uint64_t count = 1);

private:
    bool sendEvent(unsigned long command, uint64_t count);

    int fd_ = -1;
};

}  // namespace streamer

#endif
