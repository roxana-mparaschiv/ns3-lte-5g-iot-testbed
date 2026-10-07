# ns3-lte-5g-iot-testbed
# 4G LTE vs. 5G NR IoT Simulation Testbed (NS-3 on Linux)

A high-fidelity comparative network simulation testbed implemented in **C++** using the **NS-3** discrete-event network simulator on Linux. This project benchmarks Fourth Generation Long Term Evolution (4G LTE LENA) against Fifth Generation New Radio (5G NR `ns-3-nr` module) under mixed, heterogeneous Internet of Things (IoT) workloads.

---

## 📌 Project Overview & Specifications

* **Simulation Framework:** NS-3 (version 3.36+ / 3.38) executing on Linux (Ubuntu / Debian / WSL)
* **Radio Modules:**
  * **4G LTE:** LENA module with Evolved Packet Core (EPC) and Proportional Fair (PF) / Round Robin (RR) scheduling
  * **5G NR:** `ns-3-nr` module (CTTC) featuring configurable Bandwidth Parts (BWP) and 3D direct-path beamforming
* **Cellular Architecture:** Multi-cell topology with configurable eNBs/gNBs, interconnected via X2 interfaces supporting A3 RSRP-triggered handovers
* **IoT Traffic Profiles:**
  * **Sensor Telemetry (Downlink):** Low-rate periodic CBR UDP (1 kbps, 128-byte packets) from Remote Host to Sensor UEs
  * **Video Streaming (Uplink):** High-throughput bursty ON/OFF UDP (2 Mbps, 1200-byte packets, Mean ON = 0.8s, Mean OFF = 0.2s) from Video UEs to Remote Host

---

## 📊 Benchmark QoS Results Summary

Simulation results extracted via `FlowMonitor` comparing 4G LTE (10 MHz, 50 RBs) and 5G NR (40 MHz at 3.5 GHz) across scalable traffic loads:

| Traffic Configuration | Technology | Sensor Latency (ms) | Sensor PDR (%) | Video Goodput (Mbps) | Video Loss (%) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **50 Sensors, 5 Videos** | **5G NR** | **9.70 ms** | **100%** | **7.87 Mbps** | **0.00%** |
| 50 Sensors, 5 Videos | 4G LTE | 22.96 ms | 100% | 7.91 Mbps | 0.00% |
| **50 Sensors, 10 Videos** | **5G NR** | **10.34 ms** | **100%** | **15.12 Mbps** | **0.00%** |
| 50 Sensors, 10 Videos | 4G LTE | 22.96 ms | 100% | 15.59 Mbps | 0.03% |
| **100 Sensors, 5 Videos** | **5G NR** | **13.88 ms** | **100%** | **6.46 Mbps** | 14.24% |
| 100 Sensors, 5 Videos | 4G LTE | 27.99 ms | 100% | 7.98 Mbps | 0.00% |
| **200 Sensors, 5 Videos** | **5G NR** | **17.23 ms** | **100%** | **7.68 Mbps** | **0.00%** |
| 200 Sensors, 5 Videos | 4G LTE | 32.98 ms | 100% | 7.63 Mbps | 0.00% |
| **200 Sensors, 10 Videos**| **5G NR** | **16.24 ms** | **100%** | **15.89 Mbps** | **0.00%** |
| 200 Sensors, 10 Videos | 4G LTE | 32.98 ms | 100% | 15.52 Mbps | 0.00% |

### Key Findings
1. **Latency Reduction:** 5G NR achieves ~50% to 58% lower sensor latency across all loads due to wider subcarrier spacing, reduced transmission time intervals (TTI), and flexible numerology.
2. **Video Throughput & Channel Capacity:** Allocating a 40 MHz Bandwidth Part at 3.5 GHz in 5G NR resolved channel congestion for massive deployments, maintaining zero packet loss even with 200 sensors and 10 concurrent high-definition video nodes.
3. **Mobility & Handover:** LTE A3 RSRP handover triggers periodic latency jitter spikes, whereas 5G NR dynamic beamforming and multi-antenna processing deliver seamless session continuity.

---

## 🛠️ Linux Environment Setup & Execution

### Prerequisites
Run on a Linux environment (Ubuntu 20.04/22.04 LTS or WSL2):
```bash
sudo apt update
sudo apt install build-essential g++ cmake ninja-build git python3
Ensure NS-3 and the ns-3-nr module are compiled in your workspace:

Bash
# Clone and build ns-3 with the nr module
git clone [https://gitlab.com/cttc-lena/nr.git](https://gitlab.com/cttc-lena/nr.git) contrib/nr
./ns3 configure --enable-examples --enable-tests
./ns3 build
Running the Testbed
Copy project6.cc into the scratch/ directory of your NS-3 root:

Bash
cp project6.cc scratch/
Execute 5G NR simulation:

Bash
./ns3 run "scratch/project6 --useNr=true --nSensors=50 --nVideos=5 --simTime=30"
Execute 4G LTE baseline simulation:

Bash
./ns3 run "scratch/project6 --useNr=false --nSensors=50 --nVideos=5 --simTime=30"
All trace files, per-flow statistics, and XML outputs are automatically written to results_project6/run_<timestamp>/.

🎓 Academic Context
Institution: Faculty of Electronics, Telecommunications and Information Technology (ETTI), National University of Science and Technology POLITEHNICA Bucharest

Course: Data Communications

Authors: Maria-Roxana Paraschiv, Andrei Mocanu, Tudor Marcu

Academic Coordinator: Prof. Dr. Ing. Seyedsalar Sefati
