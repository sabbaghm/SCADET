# SCADET block diagram
![SCADET block diagram](https://github.com/sabbaghm/SCADET/blob/master/figures/SCADET_BD.png)
# Binary Instrumentaion and trace acquisition
## Setup
* **OS:** Ubuntu 16.04.1 operating system (Linux kernel version of 4.13.0-38-generic)
* **CPU:** Intel Skylake core-i7 6700 CPU
* **Main memory:** 16GB RAM
* **Caches:** 16-way 8MB L3 cache, 8-way 32KB L1 I-cache, and 8-way 32KB D-cache
All of our trace acquisitions are performed when regular background and foreground processes such as OS scheduling and user applications (browsers, editors, network applications etc.), were running at the same time. This added real noise to our traces.
Note: we expect our instrumentation tool to be functional accross various x86 64-bit system configurations.
## Pintool instrumentation
We used *Intel pin* version *3.4-97438-gf90d1f746-gcc-linux*. 
For i-cache instrumentation please use the [itrace.cpp](https://github.com/sabbaghm/SCADET/blob/master/src/instrumentation/itrace.cpp).
For d-cache instrumentation please use the [dtrace.cpp](https://github.com/sabbaghm/SCADET/blob/master/src/instrumentation/dtrace.cpp).

For building and running the pintool for d-cache analysis, do:
```shell
cp src/instrumentation/dtrace.cpp pin-3.4-97438-gf90d1f746-gcc-linux/source/tools/ManualExamples/
cd pin-3.4-97438-gf90d1f746-gcc-linux/source/tools/ManualExamples/
make TARGET=intel64 obj-intel64/dtrace.so
cd ../../../
./pin -t source/tools/ManualExamples/obj-intel64/dtrace.so -o <output_file_name> -- <target_executable_program> <program arguments>
For example:
./pin -t source/tools/ManualExamples/obj-intel64/dtrace.so -o traces/l1d.bin -- mastik/demo/L1-capture 100
```

Similarly you can launch the instrumentation and trace acquistions for i-cache analysis by building *itrace.so*.

# Analysis
After capturing the traces, copy them to the computing server/cluster.
## Setup
* **Spark configuration:** 
Apache Spark platform version 1.4.1 or 2.3.2 (Hadoop 2.4 or 2.7) and python 2.7.15.
1 master node with 5 worker nodes. 50GB driver memory, 50GB executor memory, 20GB maximum result size.
* **CPU:** Intel Xeon CPU E5-2690 v3 2.6GHz, 48 logical cores
* **Main memory:** 128GB RAM
## Pyspark analysis
```shell
spark-submit --master spark://<master_node_ip>:<port> --executor-memory 50G --driver-memory 50G SCADET.py --mode <READ/WRITE> <L1I/L1D/LLC> <trace>
For example:
spark-submit --master spark://<master_node_ip>:<port> --executor-memory 50G --driver-memory 50G SCADET.py --mode READ L1D traces/l1d.bin
```
