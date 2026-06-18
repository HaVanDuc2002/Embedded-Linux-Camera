#ifndef CAMERA_MONITOR_UAPI_H
#define CAMERA_MONITOR_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CAMERA_MONITOR_DEVICE_PATH "/dev/camera_monitor"

enum camera_monitor_state {
	CAMERA_MONITOR_STATE_STOPPED = 0,
	CAMERA_MONITOR_STATE_RUNNING = 1,
	CAMERA_MONITOR_STATE_ERROR = 2,
};

struct camera_monitor_stats {
	__u64 frames_captured;
	__u64 frames_dropped;
	__u64 capture_errors;
	__u64 last_update_ns;
	__u64 last_error_ns;
	__u32 state;
	__u32 reserved;
};

#define CAMERA_MONITOR_IOC_MAGIC 'M'
#define CAMERA_MONITOR_IOC_GET_STATS \
	_IOR(CAMERA_MONITOR_IOC_MAGIC, 0x00, struct camera_monitor_stats)
#define CAMERA_MONITOR_IOC_RESET_COUNTERS \
	_IO(CAMERA_MONITOR_IOC_MAGIC, 0x01)
#define CAMERA_MONITOR_IOC_FRAME_CAPTURED \
	_IO(CAMERA_MONITOR_IOC_MAGIC, 0x02)
#define CAMERA_MONITOR_IOC_FRAME_DROPPED \
	_IO(CAMERA_MONITOR_IOC_MAGIC, 0x03)
#define CAMERA_MONITOR_IOC_CAPTURE_ERROR \
	_IO(CAMERA_MONITOR_IOC_MAGIC, 0x04)
#define CAMERA_MONITOR_IOC_SET_STATE \
	_IOW(CAMERA_MONITOR_IOC_MAGIC, 0x05, __u32)

#endif
