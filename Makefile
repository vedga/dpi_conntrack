obj-m += dpi_conntrack.o

dpi_conntrack-objs :=				\
	./src/module.o				\
	./src/netns.o				\
	./src/procfs.o				\
	./src/dpi_conntrack_file.o
	
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
