CC      = gcc
HIDAPI  = third_party/hidapi/src/hid.c
CFLAGS  = -I third_party/hidapi/include -static -lsetupapi

all: bin/rapoo-battery.exe bin/rapoo-battery-gui.exe

bin/rapoo-battery.exe: src/rapoo-battery.c $(HIDAPI) | bin
	$(CC) $^ -o $@ $(CFLAGS)

bin/rapoo-battery-gui.exe: src/rapoo-battery-gui.c $(HIDAPI) | bin
	$(CC) $^ -o $@ $(CFLAGS) -lgdi32 -mwindows -fexec-charset=GBK

bin:
	mkdir -p bin

clean:
	rm -f bin/*.exe

.PHONY: all clean
