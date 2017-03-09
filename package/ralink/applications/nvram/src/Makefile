exe := nvram
objs := nvram.o crc32.o flash.o
CFLAGS += -Wall

ifeq ($(CONFIG_NVRAM_MTD_NAME),y)
CFLAGS += -DNVRAM_MTD_NAME=$(CONFIG_NVRAM_MTD_NAME)
endif


$(exe) : $(objs)
	$(CC) $(CFLAGS) -o $(exe) $(objs)

$(objs): %.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm $(objs) $(exe)
