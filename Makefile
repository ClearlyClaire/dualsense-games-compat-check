all: dualsense-games-compat-check.exe

dualsense-games-compat-check.exe: dualsense-games-compat-check.c
	x86_64-w64-mingw32-gcc dualsense-games-compat-check.c -lole32 -lsetupapi -lhid -o dualsense-games-compat-check.exe

clean:
	rm -f dualsense-games-compat-check.exe

.PHONY: all clean
