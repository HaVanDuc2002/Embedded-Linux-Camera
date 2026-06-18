#include "camera_monitor_uapi.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* stateName(uint32_t state) {
    switch (state) {
        case CAMERA_MONITOR_STATE_STOPPED: return "stopped";
        case CAMERA_MONITOR_STATE_RUNNING: return "running";
        case CAMERA_MONITOR_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

static bool printStats(int fd) {
    camera_monitor_stats stats{};
    if (ioctl(fd, CAMERA_MONITOR_IOC_GET_STATS, &stats) < 0) {
        std::fprintf(stderr, "GET_STATS failed: %s\n", std::strerror(errno));
        return false;
    }

    std::printf("state: %s\n", stateName(stats.state));
    std::printf("frames_captured: %llu\n",
                static_cast<unsigned long long>(stats.frames_captured));
    std::printf("frames_dropped: %llu\n",
                static_cast<unsigned long long>(stats.frames_dropped));
    std::printf("capture_errors: %llu\n",
                static_cast<unsigned long long>(stats.capture_errors));
    std::printf("last_update_ns: %llu\n",
                static_cast<unsigned long long>(stats.last_update_ns));
    std::printf("last_error_ns: %llu\n",
                static_cast<unsigned long long>(stats.last_error_ns));
    return true;
}

int main(int argc, char* argv[]) {
    const char* command = argc > 1 ? argv[1] : "stats";
    if (std::strcmp(command, "stats") != 0 &&
        std::strcmp(command, "reset") != 0 &&
        std::strcmp(command, "watch") != 0) {
        std::fprintf(stderr, "Usage: %s [stats|reset|watch]\n", argv[0]);
        return 2;
    }

    int fd = open(CAMERA_MONITOR_DEVICE_PATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "Cannot open %s: %s\n",
                     CAMERA_MONITOR_DEVICE_PATH, std::strerror(errno));
        return 1;
    }

    int result = 0;
    if (std::strcmp(command, "reset") == 0) {
        if (ioctl(fd, CAMERA_MONITOR_IOC_RESET_COUNTERS) < 0) {
            std::fprintf(stderr, "RESET_COUNTERS failed: %s\n",
                         std::strerror(errno));
            result = 1;
        }
    } else if (std::strcmp(command, "watch") == 0) {
        std::puts("Waiting for camera errors...");
        while (true) {
            pollfd descriptor{fd, POLLPRI | POLLERR, 0};
            int ready = poll(&descriptor, 1, -1);
            if (ready < 0) {
                if (errno == EINTR) continue;
                std::fprintf(stderr, "poll failed: %s\n", std::strerror(errno));
                result = 1;
                break;
            }
            if (!printStats(fd)) {
                result = 1;
                break;
            }
            std::fflush(stdout);
        }
    } else if (!printStats(fd)) {
        result = 1;
    }

    close(fd);
    return result;
}
