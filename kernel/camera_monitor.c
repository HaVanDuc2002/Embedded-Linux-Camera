// SPDX-License-Identifier: GPL-2.0-only

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "camera_monitor_uapi.h"

#define CAMERA_MONITOR_NAME "camera_monitor"

struct camera_monitor_device {
	struct miscdevice miscdev;
	spinlock_t lock;
	struct camera_monitor_stats stats;
	wait_queue_head_t error_wait;
	atomic64_t error_generation;
	struct dentry *debugfs_dir;
};

struct camera_monitor_file {
	struct camera_monitor_device *monitor;
	atomic64_t seen_error_generation;
};

static struct platform_device *camera_monitor_pdev;
static bool auto_create = true;
module_param(auto_create, bool, 0444);
MODULE_PARM_DESC(auto_create,
	"Create a platform device when no device-tree node is used");

static const char *camera_monitor_state_name(u32 state)
{
	switch (state) {
	case CAMERA_MONITOR_STATE_STOPPED:
		return "stopped";
	case CAMERA_MONITOR_STATE_RUNNING:
		return "running";
	case CAMERA_MONITOR_STATE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}

static bool camera_monitor_valid_state(u32 state)
{
	return state <= CAMERA_MONITOR_STATE_ERROR;
}

static void camera_monitor_record_event(struct camera_monitor_device *monitor,
					unsigned int command)
{
	unsigned long irq_flags;
	bool notify_error = false;

	spin_lock_irqsave(&monitor->lock, irq_flags);
	switch (command) {
	case CAMERA_MONITOR_IOC_FRAME_CAPTURED:
		monitor->stats.frames_captured++;
		break;
	case CAMERA_MONITOR_IOC_FRAME_DROPPED:
		monitor->stats.frames_dropped++;
		break;
	case CAMERA_MONITOR_IOC_CAPTURE_ERROR:
		monitor->stats.capture_errors++;
		monitor->stats.state = CAMERA_MONITOR_STATE_ERROR;
		monitor->stats.last_error_ns = ktime_get_ns();
		notify_error = true;
		break;
	}
	monitor->stats.last_update_ns = ktime_get_ns();
	spin_unlock_irqrestore(&monitor->lock, irq_flags);

	if (notify_error) {
		atomic64_inc(&monitor->error_generation);
		wake_up_interruptible_poll(&monitor->error_wait,
					   EPOLLPRI | EPOLLERR);
	}
}

static void camera_monitor_snapshot(struct camera_monitor_device *monitor,
				    struct camera_monitor_stats *stats)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&monitor->lock, irq_flags);
	*stats = monitor->stats;
	spin_unlock_irqrestore(&monitor->lock, irq_flags);
}

static int camera_monitor_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct camera_monitor_device *monitor;
	struct camera_monitor_file *ctx;

	monitor = container_of(miscdev, struct camera_monitor_device, miscdev);
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->monitor = monitor;
	atomic64_set(&ctx->seen_error_generation,
		     atomic64_read(&monitor->error_generation));
	file->private_data = ctx;
	return nonseekable_open(inode, file);
}

static int camera_monitor_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t camera_monitor_read(struct file *file, char __user *buffer,
				   size_t count, loff_t *offset)
{
	struct camera_monitor_file *ctx = file->private_data;
	struct camera_monitor_stats stats;

	if (*offset != 0)
		return 0;
	if (count < sizeof(stats))
		return -EINVAL;

	camera_monitor_snapshot(ctx->monitor, &stats);
	if (copy_to_user(buffer, &stats, sizeof(stats)))
		return -EFAULT;

	atomic64_set(&ctx->seen_error_generation,
		     atomic64_read(&ctx->monitor->error_generation));
	*offset = sizeof(stats);
	return sizeof(stats);
}

static long camera_monitor_ioctl(struct file *file, unsigned int command,
				 unsigned long argument)
{
	struct camera_monitor_file *ctx = file->private_data;
	struct camera_monitor_device *monitor = ctx->monitor;
	struct camera_monitor_stats stats;
	unsigned long irq_flags;
	u32 state;

	switch (command) {
	case CAMERA_MONITOR_IOC_GET_STATS:
		camera_monitor_snapshot(monitor, &stats);
		if (copy_to_user((void __user *)argument, &stats, sizeof(stats)))
			return -EFAULT;
		atomic64_set(&ctx->seen_error_generation,
			     atomic64_read(&monitor->error_generation));
		return 0;

	case CAMERA_MONITOR_IOC_RESET_COUNTERS:
		spin_lock_irqsave(&monitor->lock, irq_flags);
		monitor->stats.frames_captured = 0;
		monitor->stats.frames_dropped = 0;
		monitor->stats.capture_errors = 0;
		monitor->stats.last_update_ns = ktime_get_ns();
		monitor->stats.last_error_ns = 0;
		spin_unlock_irqrestore(&monitor->lock, irq_flags);
		atomic64_set(&ctx->seen_error_generation,
			     atomic64_read(&monitor->error_generation));
		return 0;

	case CAMERA_MONITOR_IOC_FRAME_CAPTURED:
	case CAMERA_MONITOR_IOC_FRAME_DROPPED:
	case CAMERA_MONITOR_IOC_CAPTURE_ERROR:
		camera_monitor_record_event(monitor, command);
		return 0;

	case CAMERA_MONITOR_IOC_SET_STATE:
		if (copy_from_user(&state, (void __user *)argument, sizeof(state)))
			return -EFAULT;
		if (!camera_monitor_valid_state(state))
			return -EINVAL;

		spin_lock_irqsave(&monitor->lock, irq_flags);
		monitor->stats.state = state;
		monitor->stats.last_update_ns = ktime_get_ns();
		spin_unlock_irqrestore(&monitor->lock, irq_flags);
		return 0;

	default:
		return -ENOTTY;
	}
}

static __poll_t camera_monitor_poll(struct file *file, poll_table *wait)
{
	struct camera_monitor_file *ctx = file->private_data;
	u64 current_generation;

	poll_wait(file, &ctx->monitor->error_wait, wait);
	current_generation = atomic64_read(&ctx->monitor->error_generation);
	if (current_generation != atomic64_read(&ctx->seen_error_generation))
		return EPOLLPRI | EPOLLERR;

	return 0;
}

static const struct file_operations camera_monitor_fops = {
	.owner = THIS_MODULE,
	.open = camera_monitor_open,
	.release = camera_monitor_release,
	.read = camera_monitor_read,
	.unlocked_ioctl = camera_monitor_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = camera_monitor_ioctl,
#endif
	.poll = camera_monitor_poll,
	.llseek = no_llseek,
};

static int camera_monitor_debugfs_show(struct seq_file *seq, void *unused)
{
	struct camera_monitor_device *monitor = seq->private;
	struct camera_monitor_stats stats;

	camera_monitor_snapshot(monitor, &stats);
	seq_printf(seq, "state: %s\n", camera_monitor_state_name(stats.state));
	seq_printf(seq, "state_value: %u\n", stats.state);
	seq_printf(seq, "frames_captured: %llu\n", stats.frames_captured);
	seq_printf(seq, "frames_dropped: %llu\n", stats.frames_dropped);
	seq_printf(seq, "capture_errors: %llu\n", stats.capture_errors);
	seq_printf(seq, "last_update_ns: %llu\n", stats.last_update_ns);
	seq_printf(seq, "last_error_ns: %llu\n", stats.last_error_ns);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(camera_monitor_debugfs);

static int camera_monitor_probe(struct platform_device *pdev)
{
	struct camera_monitor_device *monitor;
	int ret;

	monitor = devm_kzalloc(&pdev->dev, sizeof(*monitor), GFP_KERNEL);
	if (!monitor)
		return -ENOMEM;

	spin_lock_init(&monitor->lock);
	init_waitqueue_head(&monitor->error_wait);
	atomic64_set(&monitor->error_generation, 0);
	monitor->stats.state = CAMERA_MONITOR_STATE_STOPPED;
	monitor->stats.last_update_ns = ktime_get_ns();

	monitor->miscdev.minor = MISC_DYNAMIC_MINOR;
	monitor->miscdev.name = CAMERA_MONITOR_NAME;
	monitor->miscdev.fops = &camera_monitor_fops;
	monitor->miscdev.parent = &pdev->dev;
	monitor->miscdev.mode = 0660;

	ret = misc_register(&monitor->miscdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register misc device\n");

	monitor->debugfs_dir = debugfs_create_dir(CAMERA_MONITOR_NAME, NULL);
	if (!IS_ERR_OR_NULL(monitor->debugfs_dir))
		debugfs_create_file("stats", 0444, monitor->debugfs_dir, monitor,
				    &camera_monitor_debugfs_fops);
	else
		monitor->debugfs_dir = NULL;

	platform_set_drvdata(pdev, monitor);
	dev_info(&pdev->dev, "registered /dev/%s\n", CAMERA_MONITOR_NAME);
	return 0;
}

static int camera_monitor_remove(struct platform_device *pdev)
{
	struct camera_monitor_device *monitor = platform_get_drvdata(pdev);

	debugfs_remove_recursive(monitor->debugfs_dir);
	misc_deregister(&monitor->miscdev);
	return 0;
}

static const struct of_device_id camera_monitor_of_match[] = {
	{ .compatible = "aesd,camera-monitor" },
	{ }
};
MODULE_DEVICE_TABLE(of, camera_monitor_of_match);

static struct platform_driver camera_monitor_driver = {
	.probe = camera_monitor_probe,
	.remove = camera_monitor_remove,
	.driver = {
		.name = CAMERA_MONITOR_NAME,
		.of_match_table = camera_monitor_of_match,
	},
};

static int __init camera_monitor_init(void)
{
	int ret;

	ret = platform_driver_register(&camera_monitor_driver);
	if (ret)
		return ret;

	if (!auto_create)
		return 0;

	camera_monitor_pdev = platform_device_register_simple(
		CAMERA_MONITOR_NAME, PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(camera_monitor_pdev)) {
		ret = PTR_ERR(camera_monitor_pdev);
		platform_driver_unregister(&camera_monitor_driver);
		return ret;
	}

	return 0;
}

static void __exit camera_monitor_exit(void)
{
	if (auto_create)
		platform_device_unregister(camera_monitor_pdev);
	platform_driver_unregister(&camera_monitor_driver);
}

module_init(camera_monitor_init);
module_exit(camera_monitor_exit);

MODULE_AUTHOR("Ha Van Duc");
MODULE_DESCRIPTION("Camera streamer statistics and error notification driver");
MODULE_LICENSE("GPL");
