obj-m += camera_monitor.o

ccflags-y += -I$(src)/../include

.PHONY: all clean

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) clean
