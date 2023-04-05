# MIT License

# Copyright (c) 2022 Duke University. All rights reserved.

# See LICENSE for license information

import argparse
from distutils import core
from email.encoders import encode_quopri
import subprocess
import shutil
import json
import os
from husky_config import *

TE_SERVER_CMD_FMT = "ssh {}@{} -n -f \'for i in {{{}..{}}}; do {} --server --dev={} --port=$i --gid={} --tos={} --run_infinitely {} >/dev/null & done\'"
TE_CLIENT_CMD_FMT = "ssh {}@{} -n -f \'for i in {{{}..{}}}; do {} --dev={} --port=$i --gid={} --tos={} --run_infinitely {} >/dev/null & done\'"
PERF_SERVER_CMD_FMT = "ssh {}@{} -n -f \'for i in {{{}..{}}}; do {} -d {} -x {} -s {} -q {} -p $i --run_infinitely --report_gbits -R -F --tos {} >/dev/null  & done \'"
PERF_CLIENT_CMD_FMT = "ssh {}@{} -n -f \'for i in {{{}..{}}}; do {} -d {} -x {} -s {} -l {} -n {} -q {} -p $i --run_infinitely --report_gbits -R -F --tos {} {} >/dev/null & done \'"

CTRL_CMD_FMT = ""
# For RDMA part
# Victim Traffics (21 synthetic)


class PerfTestParam:
    def __init__(self, name:str, qp_num: int, core_num: int, req_size: int, batch_size: int):
        self.name = name
        self.qp_num = qp_num
        self.core_num = core_num
        self.req_size = req_size
        self.batch_size = batch_size


class TeTestParam:
    def __init__(self, name:str, qp_num: int, core_num: int, send_wq_depth: int, send_batch: int, 
                    recv_wq_depth: int, recv_batch: int, mr_num: int, buf_num: int, buf_size: int):
        self.name = name
        self.qp_num = qp_num
        self.core_num = core_num
        self.send_wq_depth = send_wq_depth
        self.send_batch = send_batch
        self.recv_wq_depth = recv_wq_depth
        self.recv_batch = recv_batch
        self.mr_num = mr_num
        self.buf_num = buf_num
        self.buf_size = buf_size
    
    def ToStr(self):
        k_v = self.__dict__
        command_str = ""
        for k in k_v.keys():
            if k == "name" or k == "core_num":
                continue
            command_str += " --{}={}".format(k, k_v[k])
        return command_str

class CtrlTestParam:
    def __init__(self, name:str, alloc:int, middle:int, num_pd: int, num_buffer:int, mr_size:int, memalign:int, thread:int):
        self.name = name
        self.alloc = alloc
        self.middle = middle
        self.num_pd = num_pd
        self.num_buffer = num_buffer
        self.mr_size = mr_size
        self.memalign = memalign
        self.thread = thread
    
    def ToStr(self):
        k_v = self.__dict__
        command_str = "--test=mr"
        for k in k_v.keys():
            if k == "name":
                continue
            command_str += " --{}={}".format(k, k_v[k])
        return command_str

def CheckConfig(config : dict):
    try:
        if (len(config[VICTIM_DATA_IP_LIST]) != 2):
            return False
        if (len(config[ATTACKER_DATA_IP_LIST]) != 2):
            return False
        print ("Victim Sender: {}@{}".format(config[VICTIM_USER_NAME], config[VICTIM_DATA_IP_LIST][SEND_IDX]))
        print ("Victim Recver: {}@{}".format(config[VICTIM_USER_NAME], config[VICTIM_DATA_IP_LIST][RECEIVE_IDX]))
        print ("Attacker Sender: {}@{}".format(config[ATTACKER_USER_NAME], config[ATTACKER_DATA_IP_LIST][SEND_IDX]))
        print ("Attacker Recver: {}@{}".format(config[ATTACKER_USER_NAME], config[ATTACKER_DATA_IP_LIST][RECEIVE_IDX]))
        print ("Alpha is: {} ".format(config[ALPHA]))
        print ("Scripts Folder: {}".format(config[DIRECTORY]))
        print ("Verbose: {}".format(config[VERBOSE]))
    except Exception as e:
        print (e)
        return False
    return True

def AppendToFile(filename: str, content: str):
    with open(filename, "a") as f:
        f.write(content + "\n")
        # TODO: make sleep somewhere else
        f.write("sleep 1\n")

def GenerateBWAttacker(config: dict):
    folder = config[DIRECTORY]
    username = config[ATTACKER_USER_NAME]
    receiver = config[ATTACKER_DATA_IP_LIST][RECEIVE_IDX]
    mgmt_sender = config[ATTACKER_MGMT_IP_LIST][SEND_IDX]
    mgmt_receiver = config[ATTACKER_MGMT_IP_LIST][RECEIVE_IDX]
    device = config[ATTACKER_DEVICE]
    gid = config[ATTACKER_GID]
    start_port = config[ATTACKER_PORT]
    tos = config[ATTACKER_TOS]
    params = [PerfTestParam("BW-1MB", 16, 1, 1048576, 128), PerfTestParam("BW-4KB", 16, 4, 4096, 128)]
    opcodes = ["ib_write_bw", "ib_read_bw", "ib_send_bw"]
    for param in params: 
        end_port = start_port + param.core_num - 1
        for op in opcodes:
            server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, op, device, 
                                                gid, param.req_size, param.qp_num, tos)
            client_cmd = PERF_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, op, device,
                                               gid, param.req_size, param.batch_size, max(param.batch_size, 5), param.qp_num, tos, receiver)
            AppendToFile("{}/attacker/BW/{}_{}.sh".format(folder, param.name, op), server_cmd)
            AppendToFile("{}/attacker/BW/{}_{}.sh".format(folder, param.name, op), client_cmd)

def GeneratePCIeAttacker(config: dict):
    folder = config[DIRECTORY]
    username = config[ATTACKER_USER_NAME]
    sender = config[ATTACKER_DATA_IP_LIST][SEND_IDX]
    receiver = config[ATTACKER_DATA_IP_LIST][RECEIVE_IDX]
    mgmt_sender = config[ATTACKER_MGMT_IP_LIST][SEND_IDX]
    mgmt_receiver = config[ATTACKER_MGMT_IP_LIST][RECEIVE_IDX]
    device = config[ATTACKER_DEVICE]
    gid = config[ATTACKER_GID]
    start_port = config[ATTACKER_PORT]
    tos = config[ATTACKER_TOS]
    te_pcie_param = TeTestParam("PCIe-size", 16, 6, 1024, 1, 1024, 0, 1, 1, 4096)
    requests = ["w_1_257", "w_1_129"]
    for req in requests:
        end_port = start_port + te_pcie_param.core_num - 1
        server_cmd = TE_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, TE_ENGINE, device, gid, tos, te_pcie_param.ToStr())
        client_cmd = TE_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, TE_ENGINE, device, gid, tos, te_pcie_param.ToStr() + " --request={} --connect={}".format(req, receiver))
        AppendToFile("{}/attacker/PCIe/{}_{}.sh".format(folder, te_pcie_param.name, req), server_cmd)
        AppendToFile("{}/attacker/PCIe/{}_{}.sh".format(folder, te_pcie_param.name, req), client_cmd)
    op = "ib_write_bw"
    perf_pcie_param = PerfTestParam("PCIe-loopback", 8, 1, 1048576, 1)
    end_port = start_port + perf_pcie_param.core_num
    # Receiver side loopback
    server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, op, device, gid, perf_pcie_param.req_size, perf_pcie_param.qp_num, tos)
    client_cmd = PERF_CLIENT_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, op, device, gid, perf_pcie_param.req_size, perf_pcie_param.batch_size, max(perf_pcie_param.batch_size, 5), perf_pcie_param.qp_num, tos, receiver)
    AppendToFile("{}/attacker/PCIe/{}_{}.sh".format(folder, perf_pcie_param.name, "receiver"), server_cmd)
    AppendToFile("{}/attacker/PCIe/{}_{}.sh".format(folder, perf_pcie_param.name, "receiver"), client_cmd)
    # Sender side loopback
    server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_sender, start_port, end_port, op, device, gid, perf_pcie_param.req_size, perf_pcie_param.qp_num, tos)
    client_cmd = PERF_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, op, device, gid, perf_pcie_param.req_size, perf_pcie_param.batch_size, max(perf_pcie_param.batch_size, 5), perf_pcie_param.qp_num, tos, sender)
    AppendToFile("{}/attacker/PCIe/{}_{}.sh".format(folder, perf_pcie_param.name, "sender"), server_cmd)
    AppendToFile("{}/attacker/PCIe/{}_{}.sh".format(folder, perf_pcie_param.name, "sender"), client_cmd)

def GeneratePUAttacker(config: dict):
    folder = config[DIRECTORY]
    username = config[ATTACKER_USER_NAME]
    sender = config[ATTACKER_DATA_IP_LIST][SEND_IDX]
    receiver = config[ATTACKER_DATA_IP_LIST][RECEIVE_IDX]
    mgmt_sender = config[ATTACKER_MGMT_IP_LIST][SEND_IDX]
    mgmt_receiver = config[ATTACKER_MGMT_IP_LIST][RECEIVE_IDX]
    device = config[ATTACKER_DEVICE]
    gid = config[ATTACKER_GID]
    start_port = config[ATTACKER_PORT]
    tos = config[ATTACKER_TOS]
    names = ["RC-RNR-longms", "RC-RNR-shortus", "UC-RNR", "UD-RNR"]
    args = ["--qp_type=2 --min_rnr_timer=0", "--qp_type=2 --min_rnr_timer=1", "--qp_type=3", "--qp_type=4"]
    te_rnr_param = TeTestParam("PU-RNR", 1, 1, 1, 1, 1, 0, 1, 1, 4096)
    for i in range(len(names)):
        end_port = start_port + te_rnr_param.core_num - 1
        server_cmd = TE_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, TE_ENGINE, device, gid, tos, "{} {}".format(te_rnr_param.ToStr(), args[i]))
        client_cmd = TE_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, TE_ENGINE, device, gid, tos, "{} {} --connect={} --request=s_1_4096".format(te_rnr_param.ToStr(), args[i], receiver))
        AppendToFile("{}/attacker/PU/{}_{}.sh".format(folder, te_rnr_param.name, names[i]), server_cmd)
        AppendToFile("{}/attacker/PU/{}_{}.sh".format(folder, te_rnr_param.name, names[i]), client_cmd)
    
    # Write, Send, Read, CAS, FAA short requests (sender + receiver) 5 * 2 - 10
    perf_pu_param = PerfTestParam("PU-request", 8, 8, 8, 8)
    opcodes = ["ib_write_bw", "ib_read_bw", "ib_send_bw", "ib_atomic_bw -A CMP_AND_SWAP", "ib_atomic_bw -A FETCH_AND_ADD"]
    names = ["write", "read", "send", "cas", "faa"]
    for i in range(len(names)):    
        end_port = start_port + perf_pu_param.core_num - 1
        server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, opcodes[i], device, gid, perf_pu_param.req_size, perf_pu_param.qp_num, tos)
        client_cmd = PERF_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, opcodes[i], device, gid, perf_pu_param.req_size, perf_pu_param.batch_size, max(perf_pu_param.batch_size, 5), perf_pu_param.qp_num, tos, receiver)
        AppendToFile("{}/attacker/PU/{}_{}_forward.sh".format(folder, perf_pu_param.name, names[i]), server_cmd)
        AppendToFile("{}/attacker/PU/{}_{}_forward.sh".format(folder, perf_pu_param.name, names[i]), client_cmd)
        server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_sender, start_port, end_port, opcodes[i], device, gid, perf_pu_param.req_size, perf_pu_param.qp_num, tos)
        client_cmd = PERF_CLIENT_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, opcodes[i], device, gid, perf_pu_param.req_size, perf_pu_param.batch_size, max(perf_pu_param.batch_size, 5), perf_pu_param.qp_num, tos, sender)
        AppendToFile("{}/attacker/PU/{}_{}_backward.sh".format(folder, perf_pu_param.name, names[i]), server_cmd)
        AppendToFile("{}/attacker/PU/{}_{}_backward.sh".format(folder, perf_pu_param.name, names[i]), client_cmd)

def GenerateCacheAttacker(config: dict):
    folder = config[DIRECTORY]
    username = config[ATTACKER_USER_NAME]
    receiver = config[ATTACKER_DATA_IP_LIST][RECEIVE_IDX]
    mgmt_sender = config[ATTACKER_MGMT_IP_LIST][SEND_IDX]
    mgmt_receiver = config[ATTACKER_MGMT_IP_LIST][RECEIVE_IDX]
    device = config[ATTACKER_DEVICE]
    gid = config[ATTACKER_GID]
    start_port = config[ATTACKER_PORT]
    tos = config[ATTACKER_TOS]
    # Data verbs QPC Cache
    opcodes = ["ib_write_bw", "ib_read_bw", "ib_send_bw"]
    perf_qpc_param = PerfTestParam("Cache-QPC", 32, 16, 512, 1)
    for opcode in opcodes:
        end_port = start_port + perf_qpc_param.core_num - 1
        server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, opcode, device, gid, perf_qpc_param.req_size, perf_qpc_param.qp_num, tos)
        client_cmd = PERF_CLIENT_CMD_FMT.format(
            username, mgmt_sender, start_port, end_port, opcode, device, gid, perf_qpc_param.req_size, perf_qpc_param.batch_size, max(perf_qpc_param.batch_size, 5), perf_qpc_param.qp_num, tos, receiver)
        AppendToFile("{}/attacker/Cache/{}_{}.sh".format(folder, perf_qpc_param.name, opcode), server_cmd)
        AppendToFile("{}/attacker/Cache/{}_{}.sh".format(folder, perf_qpc_param.name, opcode), client_cmd)
    # Data verbs MR (many small MR, few big MR, many big MR) Sender side and receiver side
    opcode = ["w", "r", "s"]
    mr_size = [512, 1073741824, 1048576]
    mr_num = [4096, 1, 1024]
    names = ["num", "size", "all"]
    te_access_param = TeTestParam("Cache-access_mr", 1, 4, 128, 1, 256, 32, 1, 1, 1)
    for i in range(len(mr_size)):
        for op in opcode:
            req = "{}_1_{}".format(op, mr_size[i])
            te_access_param.buf_size = mr_size[i]
            te_access_param.mr_num = mr_num[i]
            end_port = start_port + te_access_param.core_num - 1
            server_cmd = TE_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, TE_ENGINE, device, gid, tos, "{} --receive=1_{}".format(te_access_param.ToStr(), te_access_param.buf_size))
            client_cmd = TE_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, TE_ENGINE, device, gid, tos, "{} --connect={} --request={}".format(te_access_param.ToStr(), receiver, req))
            name = "{}_{}".format(names[i], op)
            AppendToFile("{}/attacker/Cache/{}_{}.sh".format(folder, te_access_param.name, name), server_cmd)
            AppendToFile("{}/attacker/Cache/{}_{}.sh".format(folder, te_access_param.name, name), client_cmd)
    ##      Memory Registration/Dereg (large MR, small MR, etc.)
    buf_num = [4096, 1024, 1]
    ctrl_reg_param = CtrlTestParam("Cache-ctrl", 5, 10, 1, 1024, 512, 1, 6)
    mr_size = [1024, 1048576, 1073741824] # 1GB can only register once
    names = ["num", "all", "size"]
    pd_num = [0, 1]
    for pd in pd_num:
        for i in range(len(mr_size)):
            if pd == 1:
                ctrl_reg_param.num_pd = buf_num[i] # each buffer is from different pd
                name = "{}_pd".format(names[i])
            else:
                name = names[i] 
            ctrl_reg_param.mr_size = mr_size[i]
            ctrl_reg_param.num_buffer = buf_num[i]
            attack_cmd = "ssh {}@{} -n -f \' for i in {{1..1}}; do {} --dev={} {} >/dev/null & done\'".format(username, mgmt_sender, CTRL_ENGINE, device, ctrl_reg_param.ToStr())
            AppendToFile("{}/attacker/Cache/{}_{}_sender.sh".format(folder, ctrl_reg_param.name, name), attack_cmd)
            attack_cmd = "ssh {}@{} -n -f \' for i in {{1..1}}; do {} --dev={} {} >/dev/null & done\'".format(username, mgmt_receiver, CTRL_ENGINE, device, ctrl_reg_param.ToStr())
            AppendToFile("{}/attacker/Cache/{}_{}_receiver.sh".format(folder, ctrl_reg_param.name, name), attack_cmd)
    ##      Recv WQE Cache - (SEND/RECV + READ)
    te_recv_test = TeTestParam("Cache-recvwqe", 8, 4, 1024, 64, 1024, 128, 1, 1, 4096)
    requests = ["s_1_4096 --qp_type=4", "s_1_4096 --qp_type=3", "s_1_4096 --qp_type=2", "r_1_16384"]
    names = ["UD_recv", "UC_recv", "RC_recv", "RC_read"]
    buffer_size = [4096, 4096, 4096, 16384]
    for i in range(len(requests)):
        req = requests[i]
        te_recv_test.buf_size = buffer_size[i]
        server_cmd = TE_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, TE_ENGINE, device, gid, tos, "{} --receive=1_{}".format(te_recv_test.ToStr(), te_recv_test.buf_size))
        client_cmd = TE_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, TE_ENGINE, device, gid, tos, "{} --connect={} --request={}".format(te_recv_test.ToStr(), receiver, req))
        name = names[i]
        AppendToFile("{}/attacker/Cache/{}_{}.sh".format(folder, te_recv_test.name, name), server_cmd)
        AppendToFile("{}/attacker/Cache/{}_{}.sh".format(folder, te_recv_test.name, name), client_cmd)

def GenerateAttackerWorkloads(config: dict):
    # 6 NIC Bandwidth Test
    GenerateBWAttacker(config)
    
    # 4 PCIe bandwidth test
    GeneratePCIeAttacker(config)

    # 14 NIC PU test
    GeneratePUAttacker(config)

    # 28 NIC Cache test: QPC, RecvWQE, MTT, MPT
    GenerateCacheAttacker(config)
    
    return 

def GenerateVictimWorkloads(config: dict):
    # Synthetic Workloads. We use perftest and modified Collie's traffic engine -> Husky traffic engine to generate victim workloads.
    folder = config[DIRECTORY]
    username = config[VICTIM_USER_NAME]
    receiver = config[VICTIM_DATA_IP_LIST][RECEIVE_IDX]
    mgmt_sender = config[VICTIM_MGMT_IP_LIST][SEND_IDX]
    mgmt_receiver = config[VICTIM_MGMT_IP_LIST][RECEIVE_IDX]
    device = config[VICTIM_DEVICE]
    gid = config[VICTIM_GID]
    start_port = config[VICTIM_PORT]
    tos = config[VICTIM_TOS]
    # Step 1: generate basic perftest workloads
    opcodes = ["ib_write_bw", "ib_send_bw", "ib_read_bw"]
    params = [PerfTestParam("large_bw", 1, 1, 65536, 128), PerfTestParam("small_tput", 16, 8, 8, 16), PerfTestParam("mtu_tput", 16, 4, 4096, 16)]
    
    for op in opcodes:
        for param in params:
            end_port = start_port + param.core_num - 1
            # Perftest has some issue with "post_list". Don't add -l on receiver side.
            server_cmd = PERF_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, op, device, gid, param.req_size, param.qp_num, tos)
            client_cmd = PERF_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, op, device, gid, param.req_size, param.batch_size, param.batch_size, param.qp_num, tos, receiver)
            AppendToFile("{}/victim/{}_{}.sh".format(folder, param.name, op), server_cmd)
            AppendToFile("{}/victim/{}_{}.sh".format(folder, param.name, op), client_cmd)

    # Step 2: generate Husky TE's workloads
    # For "cache" sensitive applications
    requests = ["s_1_512", "w_1_512", "r_1_512", "s_1_4096", "w_1_4096", "r_1_4096"]
    params = [TeTestParam("te_many_qp", 16, 6, 1, 1, 1024, 32, 1, 1, 4096), TeTestParam("te_many_mr", 6, 16, 1, 1, 1024, 32, 16, 16, 4096)]

    for param in params:
        command_str = param.ToStr()
        end_port = start_port + param.core_num - 1
        for req in requests:
            server_cmd = TE_SERVER_CMD_FMT.format(username, mgmt_receiver, start_port, end_port, TE_ENGINE, device, gid, tos, command_str)
            client_cmd = TE_CLIENT_CMD_FMT.format(username, mgmt_sender, start_port, end_port, TE_ENGINE, device, gid, tos, command_str + " --request={} --connect={}".format(req, receiver))
            AppendToFile("{}/victim/{}_{}.sh".format(folder, param.name, req), server_cmd)
            AppendToFile("{}/victim/{}_{}.sh".format(folder, param.name, req), client_cmd)

    return 

 
def FoldersInit(config: dict):
    folder = config[DIRECTORY]
    try:
        if (os.path.isdir(folder)):
            shutil.rmtree(folder)
        else:
            print ("There is no existing folder {}".format(folder))
    except Exception as e:
        print ("Remove folder {} - {}".format(folder, e))
        return
    print ("Create folder {} to store scripts...".format(folder))
    try:
        os.mkdir(folder)
        os.mkdir(folder + "/victim")
        os.mkdir(folder + "/attacker")
        os.mkdir(folder + "/attacker/BW")
        os.mkdir(folder + "/attacker/PU")
        os.mkdir(folder + "/attacker/Cache")
        os.mkdir(folder + "/attacker/PCIe")
    except Exception as e:
        print ("Create folders under {} - {}".format(folder, e))
    

def main():
    parser = argparse.ArgumentParser(description="Scripts Generator")
    parser.add_argument("--config", action="store", required=True, help="The configuration file to generate scripts. See example.json as config example.")
    args = parser.parse_args()
    config = {}
    with open(args.config, "r") as f:
        config = json.load(f)
    
    if (not CheckConfig(config)):
        print ("Configuration invalid. Please double check your config json file.")
        exit()
        
    FoldersInit(config)
    if config[VERBOSE]:
        global TE_SERVER_CMD_FMT
        global TE_CLIENT_CMD_FMT
        TE_SERVER_CMD_FMT = "ssh {}@{} -n -f \'for i in {{{}..{}}}; do {} --server --dev={} --port=$i --gid={} --tos={} --logtostderr=1 --run_infinitely {} >/dev/null & done\'"
        TE_CLIENT_CMD_FMT = "ssh {}@{} -n -f \'for i in {{{}..{}}}; do {} --dev={} --port=$i --gid={} --tos={} --logtostderr=1 --run_infinitely {} >/dev/null & done\'" 
    GenerateVictimWorkloads(config)
    GenerateAttackerWorkloads(config)

if __name__ == "__main__":
    main()