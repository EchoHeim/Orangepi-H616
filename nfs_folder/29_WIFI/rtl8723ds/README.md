# rtl8723ds
Linux driver for RTL8723DS. This repo is derived from Realtek Version
v5.1.1.5_20523_20161209_BTCOEX20161208-1212. It has been modified to
build cleanly for kernels through v4.11.

Run the following commands in the Linux terminal.

```
git clone https://github.com/lwfinger/rtl8723ds.git
cd rtl8723ds
make
sudo make install
sudo modprobe -v 8723ds

```

## b. Non Concurrent Mode
If you do not want two devices (station and an access point) separately, then follow these instructions.

Step - 1: Run the following commands in the Linux terminal. 
```
git clone https://github.com/lwfinger/rtl8723ds.git
cd rtl8723ds
nano Makefile
```

Step - 2: Find the line that contains `EXTRA_CFLAGS += -DCONFIG_CONCURRENT_MODE` and insert a `#` symbol at the beginning of that line.
 This comments that line and disables concurrent mode.

Step - 3: Now, run the following commands in the same Linux terminal.

```
make
sudo make install
sudo modprobe -v 8723ds
