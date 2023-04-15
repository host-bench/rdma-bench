# rdma-bench
Benchmark Test Suite for RDMA Networks


This README introduces the high-level information about this project. For each component/module in this test suite, please refer to the corresponding README of each folder to see the usage details (e.g., `test0-basic/README`).

## Motivations

Recent years have witnessed the prevalence of RDMA networks. People in the community are enthusiastic about developing RDMA applications, deploying RDMA at large scale, and even implementing their own specialized RDMA NICs. The growing adoption of RDMA indicates the need of a standardized benchmark test suite. We therefore implement such test suite, consisting of the following key components.

## Modules
### Test0-basic

This test module helps to quickly review the basic information about the RDMA on a host:
- Software installation information (e.g., versions)
- Performance related configurations (e.g., MTU)
- Basic performance report.

Passing this test indicates the basic correct installation of the RNIC (e.g., connectivity). To check if the host/RDMA configuration is correct, users need to edit the expected key/value pairs in the scripts accordingly. We provide some examples within the scripts.

### Test1-performance

This test module aims at answering these questions 
- Is this RDMA NIC (or subsystem, including the host) free of any performance anomaly? 
- If not, what workloads will cause what types of performance anomalies?

This test module includes 
- A traffic engine that can generate various types of data verbs workloads. 
- A search engine that actively search for potential workloads that cause performance anomalies in a given RDMA subsystem.
- A search space configuration file that describes potential workload patterns. Users can specify parameters of this config file to shrink or extend the search space accordingly.

Passing this test indicates that this RDMA subsystem is free of performance anomaly given the some restrictions (the search space configuration). This can serve as a signal indicating that this RDMA subsystem is ready to deploy in the production environment for first-party traffic, where the overall performance is the key metrics.

The design and implementation of this module, as well as the experience gained from its usage, has been documented in a paper accepted for publication at [NSDI '22](https://www.usenix.org/conference/nsdi22/presentation/kong).

### Test2-isolation

This test module aims at answering these questions - 
- Is this RDMA subsystem + the isolation solution provide adequate performance isolation for multi-tenant RDMA networks?
- If the isolation is violated, what is the root cause of such violation?

This test module includes
- An enhanced traffic engine that can generate various types of data/control verbs workloads as well as inject errors to the RDMA subsystems.
- A set of tuned traffics (attackers) that correspond to exhaustion of different RDMA microarchitecture resources.
- A set of tuned traffics (victims) that are sensitive to different RDMA microarchitecture resources usage.

Users can run each \<attacker, victim\> pair to see if a type of microarchitecture resource is isolated well by the used isolation mechanism.

Passing this test indicates that this RDMA subsystem, including the isolation mechanism, is ready for public multi-tenant environment use.

The design and implementation of this module, as well as the experience gained from its usage, has been documented in a paper accepted for publication at [NSDI '23](https://www.usenix.org/conference/nsdi23/presentation/kong).

## Current Users

We are glad that the following companies are using this rdma-bench for their RDMA development and maintainence:
<p align="center">
<img src="icon.jpg" alt="Alt text" width="600">
</p>

- Microsoft: Microsoft is currently utilizing this project to automate their RDMA testing. With the help of this project, Microsoft has uncovered and addressed several potential vulnerabilities in the NICs they are currently testing. This has helped to improve the reliability and performance of their RDMA networks.

- ByteDance: ByteDance is also utilizing this project to automate their RDMA NIC testing. By using rdma-bench, ByteDance has identified several performance issues in their RDMA networks, which they are working collaboratively with hardware vendors to address and improve.

- Nvidia: Nvidia has actively engaged with us regarding issues uncovered by this project on their Connect-X series NICs. They have used rdma-bench to reproduce the identified issues and have been responsive in addressing them, including publishing CVEs, releasing new firmware upgrades, and providing specialized NIC configurations. This project has helped Nvidia to improve the reliability, availability, and performance of their RDMA NICs.

- Intel: Intel has also reached out to us regarding issues identified by this project on their E810 series NICs. They have been actively using rdma-bench to reproduce the identified issues and have shown a strong commitment to improving their RDMA NICs by addressing them. We are working collaboratively with Intel to address these issues with the help of this project.

## Contact

If you have any technical issues using this tool or have any questions or feedback, please don't hesitate to reach out to us. You can contact us by email at xinhao.kong@duke.edu (Xinhao Kong) or by submitting a GitHub issue. We appreciate any feedback or suggestions you may have to improve this project and make it more useful to the community.

## License
This rdma-bench is under MIT license.
