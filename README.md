# Introduction

This repository contains the software artifacts for [SCADET: A Side-Channel Attack Detection Tool for Tracking Prime+Probe](http://delivery.acm.org/10.1145/3250000/3240844/a107-sabbagh.pdf?ip=155.33.134.9&id=3240844&acc=OPEN&key=AA86BE8B6928DDC7%2EC2B8A117C7A71F5A%2E4D4702B0C3E38B35%2E6D218144511F3437&__acm__=1543593400_bbb97bab0d4e725d02770bf32a2fe6d6). Please read the article for a detailed description of the SCADET methodology and the tool components.

# SCADET block diagram
![SCADET block diagram](https://github.com/sabbaghm/SCADET/blob/master/figures/SCADET_BD.png)
# Binary instrumentaion and trace acquisition
## Setup
* **OS:** Ubuntu 16.04.1 operating system (Linux kernel version of 4.13.0-38-generic)
* **CPU:** Intel Skylake core-i7 6700 CPU
* **Main memory:** 16GB RAM
* **Caches:** 16-way 8MB L3 cache, 8-way 32KB L1 I-cache, and 8-way 32KB D-cache
All of our trace acquisitions are performed when regular background and foreground processes such as OS scheduling and user applications (browsers, editors, network applications etc.), were running at the same time. This added real noise to our traces.<br /> 
Note: we expect our instrumentation tool to be functional accross various x86 64-bit system configurations.
## Pintool instrumentation
We used *Intel pin* version *3.4-97438-gf90d1f746-gcc-linux*.<br /> 
For i-cache instrumentation use the [itrace.cpp](https://github.com/sabbaghm/SCADET/blob/master/src/instrumentation/itrace.cpp).<br /> 
For d-cache instrumentation use the [dtrace.cpp](https://github.com/sabbaghm/SCADET/blob/master/src/instrumentation/dtrace.cpp).<br /> 

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

# Address analysis and pattern detection
After capturing the traces, copy them to the computing server/cluster.
## Setup
* **Spark configuration:** 
Apache Spark platform version 1.4.1 or 2.3.2 (Hadoop 2.4 or 2.7) and python 2.7.15.<br /> 
1 master node with 5 worker nodes. 50GB driver memory, 50GB executor memory, and 20GB maximum result size.
* **CPU:** Intel Xeon CPU E5-2690 v3 2.6GHz, 48 logical cores
* **Main memory:** 128GB RAM
## Pyspark analysis
```shell
spark-submit --master spark://<master_node_ip>:<port> --executor-memory 50G --driver-memory 50G SCADET.py --mode <READ/WRITE> <L1I/L1D/LLC> <trace>
For example:
spark-submit --master spark://<master_node_ip>:<port> --executor-memory 50G --driver-memory 50G SCADET.py --mode READ L1D traces/l1d.bin
```
