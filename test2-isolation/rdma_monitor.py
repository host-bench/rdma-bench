# MIT License

# Copyright (c) 2022 Duke University. All rights reserved.

# See LICENSE for license information
import subprocess
import argparse
import time
ETH_DEFAULT_STR = "rx_vport_rdma_unicast"
CHECK_RUN_TIMEOUT = 0.3

def ParseResult(results, bytes_key, packets_key):
	val = {
		bytes_key : 0.0,
		packets_key : 0.0
	}
	for r in results:
		if bytes_key in r:
			val[bytes_key] = float(r.split(' ')[-1])
		if packets_key in r:
			val[packets_key] = float(r.split(' ')[-1])
	return val

def killall():
	try:
		subprocess.check_output("killall RdmaEngine", shell=True)
	except Exception as e:
		pass
	try:
		subprocess.check_output("killall RdmaCtrlTest", shell=True)
	except Exception as e:
		pass
	try:
		subprocess.check_output("killall ib_write_bw", shell=True)
	except Exception as e:
		pass
	try:
		subprocess.check_output("killall ib_read_bw", shell=True)
	except Exception as e:
		pass
	try:
		subprocess.check_output("killall ib_send_bw",shell=True)
	except Exception as e:
		pass
	try:
		subprocess.check_output("killall ib_atomic_bw", shell=True)
	except Exception as e:
		pass
	
def check_run(name: str, target: int):
	cmd = "rdma res show qp | grep 'RTS.*{}' | wc -l".format(name)
	for i in range(10):
		try:
			ret = int(subprocess.check_output(cmd, shell=True).decode().rstrip('\n'))
			if ret >= target:
				print ("{}".format(ret))
				return 
		except Exception as e:
			print ("-1")
			return 
		time.sleep(CHECK_RUN_TIMEOUT)
	print ("-1")
	return 

def main():
	parser = argparse.ArgumentParser(description="Rdma Monitor")
	parser.add_argument("--action", action="store", default="monitor")
	parser.add_argument("--interface", action="store", help="The ethernet interface")
	parser.add_argument("--count", action="store", type=int, help="Number of seconds to monitor")
	parser.add_argument("--key", action="store", default="rx_vport_rdma_unicast_", help="The key decided by the isolation scheme")
	parser.add_argument("--run_name", action="store", type=str, default="RdmaEngine", help="The name of the running binary")
	parser.add_argument("--run_target", action="store", default=0, help="The target number of RTS QP", type=int)
	args = parser.parse_args()
	# update the global keys of the monitor metrics
	bytes_key = args.key + "_bytes"
	packets_key = args.key + "_packets"
	if args.action == "monitor":
		intf = args.interface 
		cmd = "ethtool -S {}".format(intf)
		old_time = time.time_ns()
		results = subprocess.check_output(cmd, shell=True).decode().split('\n')
		old_val = ParseResult(results, bytes_key, packets_key)
		time.sleep(args.count)
		new_time = time.time_ns()
		results = subprocess.check_output(cmd, shell=True).decode().split('\n')
		new_val = ParseResult(results, bytes_key, packets_key)
		bitrate = (new_val[bytes_key] - old_val[bytes_key]) * 8.0 / (new_time - old_time) # ->K->M->G, ->us->ms->s
		pktrate = (new_val[packets_key] - old_val[packets_key]) * 1000.0 / (new_time - old_time) # ->K->M, ->us->ms->s
		print ("{}:{}".format(bytes_key, bitrate))
		print ("{}:{}".format(packets_key, pktrate))
	elif args.action == "kill":
		killall()
	elif args.action == "check":
		name = args.run_name
		target = args.run_target
		check_run(name, target)
		return

		

if __name__ == "__main__":
	main()