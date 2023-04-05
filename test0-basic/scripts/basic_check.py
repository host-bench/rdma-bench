#!/usr/bin/python3

# This script needs to be run in privileged mode
import subprocess
import os
import argparse
import time

MED_LATENCY_BAR = 2.5
P99_LATENCY_BAR = 5
P999_LATENCY_BAR = 10

BW_BAR = 0.8 # 80% of the NIC speed should be achieved

def OUTPUT(message: str):
    print("\033[0;32;40m{}\033[0m".format(message))

def ERROR_OUTPUT(message: str):
    print("\033[1;31;40m{}\033[0m".format(message))

def SoftwareVersions() -> int:
    try:
        # Get the version of the kernel
        kernel_version = subprocess.check_output("uname -r", shell=True).decode("utf-8").strip()
        print ("Kernel version: " + kernel_version)

        # Get the version of OFED
        ofed_version = subprocess.check_output("ofed_info -s", shell=True).decode("utf-8").strip()
        print ("OFED version: " + ofed_version)

        # Get the version of ibverbs with dpkg
        libibverbs_version = subprocess.check_output("dpkg -l | grep libibverbs", shell=True).decode("utf-8").strip()
        print ("libibverbs version: ")
        print (libibverbs_version)  
    except Exception as e:
        print ("Error: " + str(e))
        return 1
    
    # Get the version of perftest. 
    try:
        perftest_version = subprocess.run(["ib_write_bw", "--version"], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()
        print ("perftest version: " + perftest_version)
    except Exception as e:
        print ("Error: " + str(e))
        print ("Perftest (e.g., ib_write_bw) may not be installed.")
    return 0

def FirmwareCheck()->int:
    print ("This currently only works for Mellanox devices...")
    try:
        # Start the mst device
        subprocess.run("mst start", shell=True, check=True)
    except Exception as e:
        print ("Mst start error: " + str(e))
        return 1

    try:
        # Check the mst status and get the device name
        mst_status = subprocess.check_output("mst status", shell=True).decode("utf-8").strip()
        print ("Mst status: ")
        print (mst_status)
        _ = mst_status.split()
        devices = []
        for i in _:
            if i.startswith("/dev"):
                devices.append(i)
        OUTPUT ("Mst devices: ")
        OUTPUT (devices)
    except Exception as e:
        print ("Get mst device names failed: " + str(e))
        return 1
    
    try:
        # Check the Firmware status of each device
        for device in devices:
            fw_status = subprocess.check_output("mlxfwmanager -d /dev/mst/mt4125_pciconf0", shell=True).decode("utf-8").strip()
            print ("Firmware status of " + device + ": ")
            print (fw_status)
    except Exception as e:
        print ("Get firmware status failed: " + str(e))
        return 1
    return 0

def BasicPerformance(devices)->int:
    if devices == [] or devices == None:
        # Get devices list
        devices = []
        try:
            _ = subprocess.check_output("ibdev2netdev", shell=True).decode("utf-8").strip().split('\n')
            for i in _:
                ib_dev = i.split()[0]
                devices.append(ib_dev)
        except Exception as e:
            print ("Get devices list failed: " + str(e))
            return 1
    OUTPUT ("Devices: " + str(devices))
    if (BasicLatency(devices)) != 0:
        return 1
    if (BasicThroughput(devices)) != 0:
        return 1
    return 0

def BasicLatency(devices)->int:
    # Test loopback performance using ib_write_lat
    print ("Testing a loopback traffic using ib_write_lat...")
    for device in devices:
        try:
            # Start a server
            subprocess.Popen(["ib_write_lat", "-s", "8", "-d", device, "-F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # Start a client
            time.sleep(1)
            client_result = subprocess.run(["ib_write_lat", "-s", "8", "-d", device, "-F", "localhost"], stdout=subprocess.PIPE)
            print (client_result.stdout.decode("utf-8").strip())
            perfs = client_result.stdout.decode("utf-8").strip().split('\n')[-2].split()
            median_lat = float(perfs[4])
            p99_lat = float(perfs[7])
            p999_lat = float(perfs[8])
            good = 1
            if median_lat > MED_LATENCY_BAR:
                ERROR_OUTPUT ("Error: Median latency is too high: " + str(median_lat))
                good = 0
            if p99_lat > P99_LATENCY_BAR:
                ERROR_OUTPUT ("Error: P99 latency is too high: " + str(p99_lat))
                good = 0
            if p999_lat > P999_LATENCY_BAR:
                ERROR_OUTPUT ("Error: P999 latency is too high: " + str(p999_lat))
                good = 0
            if good:
                OUTPUT ("Basic performance test looks good (median latency: " + str(median_lat) + " us, P99 latency: " + str(p99_lat) + " us, P999 latency: " + str(p999_lat) + " us")
                OUTPUT ("      thresholds: median latency < " + str(MED_LATENCY_BAR) + " us, P99 latency < " + str(P99_LATENCY_BAR) + " us, P999 latency < " + str(P999_LATENCY_BAR) + " us")
        except Exception as e:
            print ("ib_write_lat failed: " + str(e))
            return 1
    return 0

def BasicThroughput(devices) -> int:
    # Test loopback performance using ib_write_bw
    print ("Testing a loopback traffic using ib_write_bw...")
    for device in devices:
        try:
            # Get the speed of this NIC using ibstatus
            _ = subprocess.check_output("ibstatus " + device, shell=True).decode("utf-8").strip().split('\n')
            for i in _:
                if 'rate' in i:
                    speed = float(i.split()[1])
                    break
    
            # Start a server
            subprocess.Popen(["ib_write_bw", "-d", device, "-F", "--report_gbits"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # Start a client
            time.sleep(1)
            client_result = subprocess.run(["ib_write_bw", "-d", device, "-F", "--report_gbits", "localhost"], stdout=subprocess.PIPE)
            print (client_result.stdout.decode("utf-8").strip())
            perfs = client_result.stdout.decode("utf-8").strip().split('\n')[-2].split()
            avg_bw = float(perfs[-2])
            good = 1
            if avg_bw < speed * BW_BAR:
                ERROR_OUTPUT ("Error: Average bandwidth is too low: " + str(avg_bw))
                good = 0
            if good:
                OUTPUT ("Basic performance test looks good (average bandwidth: " + str(avg_bw) + " Gbps, speed: " + str(speed) + " Gbps")
                OUTPUT ("      thresholds: average bandwidth > " + str(BW_BAR) + " * speed")
        except Exception as e:
            print ("ib_write_bw failed: " + str(e))
            return 1
    return 0

def QoSConfig()->int:
    # Currently only support Nvidia Mellanox NICs check
    _ = subprocess.check_output("ibdev2netdev", shell=True).decode("utf-8").strip().split('\n')
    for i in _:
        ib_dev = i.split()[0]
        net_dev = i.split()[-2]
        if ib_dev.startswith("mlx"):
            OUTPUT ("Mellanox NIC detected: " + ib_dev + " / " + net_dev)
            try:
                qos_config = subprocess.check_output("mlnx_qos -i " + net_dev, shell=True).decode("utf-8").strip()
                print ("QoS config of " + net_dev + ": ")
                print (qos_config)
                dscp_setting = subprocess.check_output("cma_roce_tos -d " + ib_dev, shell=True).decode("utf-8").strip()
                OUTPUT ("Global DSCP setting of " + ib_dev + ": " + dscp_setting)
            except Exception as e:
                print ("Get QoS config failed: " + str(e))
                return 1
        else:
            OUTPUT ("Non-Mellanox NIC detected: " + ib_dev + " / " + net_dev)
            print ("Skip QoS config check for this NIC.")
    return 0


def main(args): 
    if args.sw:
        print ("Checking software status...")
        if SoftwareVersions() != 0:
            print ("Error: Necessary softwares are not installed")
            exit(1)
        OUTPUT ("Software check passed. Necessary packages are installed.")
    if args.fw:
        print ("Checking firmware status...")
        if FirmwareCheck() != 0:
            print ("Error: Firmware check failed.")
            exit(1)
        OUTPUT ("Firmware check passed. Mst tools and firmware managers are installed.")
    if args.qos:
        print ("Checking QoS config...")
        if QoSConfig() != 0:
            print ("Error: QoS config check failed.")
            exit(1)
        OUTPUT ("QoS config check over. See above output.")
    if args.perf:
        print ("Checking basic performance...")
        if BasicPerformance(args.devs) != 0:
            print ("Error: Basic performance check failed.")
            exit(1)
        OUTPUT ("Basic performance check over. See above output.")
    OUTPUT ("All checks are done...")

if __name__ == "__main__":
    if os.geteuid() != 0:
        print ("Please run this script in privileged mode.")
        exit(1)
    parser = argparse.ArgumentParser(description="Basic checks for RDMA func", add_help=True)
    parser.add_argument("--sw", help="Check the software versions", action="store_true")
    parser.add_argument("--fw", help="Check the firmware status", action="store_true")
    parser.add_argument("--perf", help="Check the basic performance", action="store_true")
    parser.add_argument("--qos", help="Check the QoS config", action="store_true")
    parser.add_argument("--devs", help="Specify the devices to check for the performance", nargs='+')
    args = parser.parse_args()
    main(args)
