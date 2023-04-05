# Husky
Benchmark Suite for RDMA Performance Isolation


## Quick Start

### Install prerequisites
~~~
sudo apt install cmake make g++ pkg-config libgoogle-glog-dev libgflags-dev
~~~


### Build and Install

~~~
cmake .;
make -j;
sudo make install;
~~~

You should see `RdmaEngine` and `RdmaCtrlTest` under current directory.
The above should be called on all machines that run test engines. An assitant script `rdma_monitor.py` need to be installed in `/tmp` to run the entire test suite.

### Generate Scripts

~~~
cd scripts;
vim example.json;
python3 scripts_gen.py --config example.json;
~~~

Edit configuration file and generate scripts following the above command. Please see below for the detailed description of this configuration file.

### Run Tests
~~~
cd scripts; # no need if you are already under this folder.
# Below are some examples to use this script

# Show the test scripts list
python3 runtest.py --config example.json --list;

# Run all <attacker, victim> combinations
python3 runtest.py --config example.json  --attacker all 

# Run all Cache attackers for all victims combinations
python3 runtest.py --config example.json --attacker Cache-all

# Run all Cache attackers for a single victim
python3 runtest.py --config example.json  --victim large_bw_ib_write_bw.sh --attacker Cache-all

# Run a particular <attacker, victim>
python3 runtest.py --config example.json  --victim large_bw_ib_write_bw.sh --attacker Cache-access_mr_all_r.sh
~~~

## Configuration

### Username and IP list
Username and IP list are for traffic set up. This test suite will call `ssh [username]@[mgmtIP] 'command'` to set up the traffic engine. The list[0] is sender and list[1] is receiver. 

The mgmt and data IP can be different (e.g., the host may have one management NIC and an RDMA NIC). 

### RDMA configurations


"VictimDevice"(and "AttackerDevice") are Infiniband device name, such as "mlx5_0". This can be acquired through `show_gids`.

"VictimGid" is the GID index used by the test suite. For RoCE, we recommend you to use RoCEv2 gid index. This can also be acquired through `show_gids`. 

Below is an example of `show_gids` result, which tells us to set `VictimGid` to be 3.
~~~
$ show_gids
DEV	PORT	INDEX	GID					IPv4  		VER	DEV
---	----	-----	---					------------  	---	---
mlx5_0	1	0	fe80:0000:0000:0000:0ac0:ebff:fe21:03da			v1	rdma0
mlx5_0	1	1	fe80:0000:0000:0000:0ac0:ebff:fe21:03da			v2	rdma0
mlx5_0	1	2	0000:0000:0000:0000:0000:ffff:c0a8:d3f2	192.168.211.242  	v1	rdma0
mlx5_0	1	3	0000:0000:0000:0000:0000:ffff:c0a8:d3f2	192.168.211.242  	v2	rdma0
n_gids_found=4
~~~

"VictimTos" is the Type of Service value of the victim (same for the attacker). You can specify this value (together with the QoS setting of the RNIC) to force the traffic to go through particular NIC queue (traffic class) and use particular NIC buffer. (Note: tos = dscp << 4 & ECN-bit).

The DSCP traffic class mapping can be obtained and set through `mlnx_qos`. Below is an example of `mlnx_qos`:

~~~
$ mlnx_qos -i rdma0
DCBX mode: OS controlled
Priority trust state: dscp
dscp2prio mapping:
	prio:0 dscp:07,06,05,04,03,02,01,00,
	prio:1 dscp:15,14,13,12,11,10,09,08,
	prio:2 dscp:23,22,21,20,19,18,17,16,
	prio:3 dscp:31,30,29,28,27,26,25,24,
	prio:4 dscp:39,38,37,36,35,34,33,32,
	prio:5 dscp:47,46,45,44,43,42,41,40,
	prio:6 dscp:55,54,53,52,51,50,49,48,
	prio:7 dscp:63,62,61,60,59,58,57,56,
... # many other outputs
~~~

"VictimPort" is the *start* port number of the victim application. `RdmaEngine` and `Perftest` may use these ports to set up RDMA connections (RDMA needs out-of-band transmission to exchange metadata for connection setup). For example, if VictimPort is set to 3001, and the victim script sets up 4 RDMA processes, [3001, 3004] will be occupied by the victim application. If the victim and the attacker are collocated on the same host (sharing the same available ports), the start port should be set different. 

### Alpha and MonitorKey

The "Alpha" in the configuration file is a little bit different from the one mentioned in paper. In this test suite, we compute the ratio of the "new performance" over the "old performance". If this ratio is smaller than the "Alpha" (new performance is worse than expectation), the test suite will consider this particular test failed. However, it could be the case that the "old performance" is better than what it should be, users should set this "Alpha" carefully or just implement their own isolation violation spec in `OneTest` of the `runtest.py`. 

The "MonitorKey" is which metric of the `ethtool -S` output will be used as the performance metric prefix. For Mellanox NICs, there are two types of monitor keys to use 
- `rx_vport_rdma_unicast` for separate virtual function (inside VM) or the bare-metal machine 
- `rx_prio[priority_id]` for separate hardware traffic class (e.g., `rx_prio0`).

The actual monitor keys used by the test suite will be `rx_vport_rdma_unicast_bytes` + `rx_vport_rdma_unicast_packets`, or `rx_prio[prio_id]_bytes` + `rx_prio[prio_id]_packets` respectively. NIC from different vendors may have different names or even different mechanisms (rather than `ethtool -S`). Users can edit `rdma_monitor.py` to support other monitoring approaches.
### Misc

"Directory" is where the testing scripts will be generated. 

"Verbose" determines whether the generated testing scripts will omit stderr and stdout output.


## Publications

The corresponding paper can be found here: 
[Understanding RDMA Microarchitecture Resources for Performance Isolation](https://www.usenix.org/conference/nsdi23/presentation/kong)

## Acknowledgement

We would like to express our gratitude to Chelsio, Intel, and NVIDIA for their technical support. In particular, we would like to extend our appreciation to Intel for providing us with valuable feedback and suggestions to enhance this test suite. We are also thankful to NVIDIA for their prompt and insightful feedback, which helped us identify the root causes of our findings and provided corresponding solutions.