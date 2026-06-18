#include "camera_monitor.hpp"
#include "log.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace streamer {

CameraMonitor::CameraMonitor(const char* device) {
    fd_ = open(device, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        LOG_WARN << "Camera monitor unavailable at " << device << ": "
                 << std::strerror(errno);
        return;
    }

    LOG_INFO << "Camera monitor connected: " << device;
}

CameraMonitor::~CameraMonitor() {
    if (fd_ >= 0) {
        setState(CAMERA_MONITOR_STATE_STOPPED);
        close(fd_);
    }
}

bool CameraMonitor::resetCounters() {
    if (fd_ < 0) {
        return false;
    }
    if (ioctl(fd_, CAMERA_MONITOR_IOC_RESET_COUNTERS) < 0) {
        LOG_WARN << "Failed to reset camera monitor counters: "
                 << std::strerror(errno);
        return false;
    }
    return true;
}

bool CameraMonitor::getStats(camera_monitor_stats& stats) {
    if (fd_ < 0) {
        return false;
    }
    if (ioctl(fd_, CAMERA_MONITOR_IOC_GET_STATS, &stats) < 0) {
        LOG_WARN << "Failed to query camera monitor: " << std::strerror(errno);
        return false;
    }
    return true;
}

bool CameraMonitor::setState(camera_monitor_state state) {
    if (fd_ < 0) {
        return false;
    }

    uint32_t value = static_cast<uint32_t>(state);
    if (ioctl(fd_, CAMERA_MONITOR_IOC_SET_STATE, &value) < 0) {
        LOG_WARN << "Failed to set camera monitor state: "
                 << std::strerror(errno);
        return false;
    }
    return true;
}

bool CameraMonitor::reportFrame(uint64_t count) {
    return sendEvent(CAMERA_MONITOR_IOC_FRAME_CAPTURED, count);
}

bool CameraMonitor::reportDrop(uint64_t count) {
    return sendEvent(CAMERA_MONITOR_IOC_FRAME_DROPPED, count);
}

bool CameraMonitor::reportError(uint64_t count) {
    return sendEvent(CAMERA_MONITOR_IOC_CAPTURE_ERROR, count);
}

bool CameraMonitor::sendEvent(unsigned long command, uint64_t count) {
    if (fd_ < 0) {
        return false;
    }

    for (uint64_t i = 0; i < count; ++i) {
        if (ioctl(fd_, command) < 0) {
            LOG_WARN << "Failed to report camera monitor event: "
                     << std::strerror(errno);
            return false;
        }
    }
    return true;
}

}  // namespace streamer
