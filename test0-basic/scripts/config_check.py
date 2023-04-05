#!/usr/bin/python3

# This script needs to be run in privileged mode

import subprocess
import os
import argparse
import re

EXPECTED_MLNX_CONFIG = {
    "PCI_WR_ORDERING": 1,
    "CNP_DSCP_P1": 48,
}

def OUTPUT(message: str):
    print("\033[0;32;40m{}\033[0m".format(message))

def ERROR_OUTPUT(message: str):
    print("\033[1;31;40m{}\033[0m".format(message))

def CheckPCIe(ib_dev, pci_dev):
    print ("Check PCIe params for " + ib_dev + " ...")
    try:
        _ = subprocess.check_output("lspci -s " + pci_dev + " -vvv", shell=True).decode('utf-8').strip().split('\n')
        for line in _:
            if "MaxReadReq" in line:
                max_read_req = int(line.strip().split()[-2])
                max_payload = int(line.strip().split()[-5])
    except Exception as e:
        print ("Get PCIe info failed: " + str(e))
        return 1
    # Suggested by Nvidia document
    OUTPUT("ib_dev: " + ib_dev + " max_payload: " + str(max_payload) + " max_read_req: " + str(max_read_req))
    if max_read_req < 512: 
        ERROR_OUTPUT("MaxReadReq (" + max_read_req + ") is less than 512. Please set to some larger value for better performance.")
    return 0
    

def CheckMTU(ib_dev):
    print ("Check MTU for " + ib_dev + " ...")
    try:
        _ = subprocess.check_output("ibv_devinfo -d " + ib_dev, shell=True).decode('utf-8').strip().split('\n')
        for line in _:
            if 'active_mtu' in line:
                mtu = int(line.strip().split()[-2])
            if 'max_mtu' in line:
                max_mtu = int(line.strip().split()[-2])
    except Exception as e:
        print ("Get MTU failed: " + str(e))
        return 1
    OUTPUT("ib_dev: " + ib_dev + " mtu: " + str(mtu) + " max_mtu: " + str(max_mtu))
    if mtu < max_mtu:
        ERROR_OUTPUT("MTU " + str(mtu) + " is not set to the maximum value. Please set it to " + str(max_mtu) + " and restart the IB device.")
    return 0
    

def CheckHwConfig(ib_dev: str):
    # Currently Mellanox Only
    if "mlx" not in ib_dev:
        print ("Only Mellanox devices are supported.")
        print ("Skip device: " + ib_dev)
        return 0
    # devices[ib_dev] = [pci_dev, net_dev, mst_dev, numa]
    devices = {}
    try:
        # Start the mst device
        subprocess.run("mst start", shell=True, check=True,  stderr=subprocess.DEVNULL, stdout= subprocess.DEVNULL)
    except Exception as e:
        print ("Mst start error: " + str(e))
        return 1
    try:
        # Check the mst status and get the device name
        _ = subprocess.check_output("mst status -v", shell=True, stderr=subprocess.DEVNULL).decode("utf-8").strip().split('\n')
        for line in range(7, len(_), 1):
            ent = _[line].split()
            dev = ent[3]
            mst_dev = ent[1]
            pci_dev = ent[2]
            net_dev = ent[4]
            numa = int(ent[5])
        devices[dev] = [pci_dev, net_dev, mst_dev, numa]
    except Exception as e:
        print ("Get mst device names failed: " + str(e))
        return 1
    try:
        _ = subprocess.check_output("mlxconfig -d " + devices[ib_dev][2] + " q", shell=True).decode("utf-8").strip().split('\n')
        print ("Mellanox device " + ib_dev + " hw/fw configuration:")
    except Exception as e:
        print ("Mlxconfig not found. Please install it.")
        return 1   
    good = 1
    for line in _:
        for key in EXPECTED_MLNX_CONFIG.keys():
            if key in line:
                OUTPUT(line)
                match = re.search(r'\d+', line.strip().split()[-1])
                value = int(match.group())
                if value != EXPECTED_MLNX_CONFIG[key]:
                    ERROR_OUTPUT("Mellanox device " + ib_dev + " configuration " + key + " is " + str(value) + " different from " + str(EXPECTED_MLNX_CONFIG[key]))
                    good = 0 
    if good:
        OUTPUT("Mellanox device " + ib_dev + " configuration is good.")
    return 0


def CheckDevice(ib_dev :str, net_dev :str, pci_dev :str):
    # Check the MTU
    if (CheckMTU(ib_dev) != 0):
        print ("MTU check failed.")
        return 1
    # Check the PCIe
    if (CheckPCIe(ib_dev, pci_dev) != 0):
        print ("PCIe check failed.")
        return 1
    if (CheckHwConfig(ib_dev) != 0):
        print ("Hardware configuration check failed.")
        return 1
    OUTPUT ("Check device {}/{}/{} over.".format(ib_dev, net_dev, pci_dev))
    return 0

def GetDevices():
    devices = {}
    try:
        _ = subprocess.check_output("ibdev2netdev -v", shell=True).decode("utf-8").strip().split('\n')
        for line in _:
            pci_dev = line.split()[0]
            ib_dev = line.split()[1]
            net_dev = line.split()[-2]
            devices[ib_dev] = [net_dev, pci_dev]
    except Exception as e:
        print ("Get devices failed: " + str(e))
        return {}
    return devices

def main(args):
    devices = GetDevices()
    if len(devices) == 0:
        print ("No devices found. Check failed.")
        exit(1)
    if args.devs is None:
        args.devs = list(devices.keys())
    for device in args.devs:
        if device not in devices.keys():
            print ("Device " + device + " not found. Check failed.")
            continue
        print ("Checking device " + device + "...")
        if (CheckDevice(device, devices[device][0], devices[device][1]) != 0):
            print ("Device " + device + " check failed.")
            continue


if __name__ == "__main__":
    if os.geteuid() != 0:
        print ("Please run this script in privileged mode.")
        exit(1)
    parser = argparse.ArgumentParser(description="Perf-related Config checks for RDMA func", add_help=True)
    parser.add_argument("--devs", help="Specify the IB devices to check for the performance", nargs='+')
    args = parser.parse_args()
    main(args)
