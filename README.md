# MultiFlowDeviceFile

Usage: 
1) Use command "make" in the Linux Terminal. 
2) Add the module using: sudo insmod multi_flow_device.ko
3) Using command "dmesg", take the Major number.
4) Test read and write using write.c and read.c in /user folder. (Compile using make command)
5) Test spawn of multiple read and write thread using spawn_thread.c 
