# MLPerf™ Client Power Measurement

MLPerf Client Power Measurement is a standardized process for evaluating the power consumption of client systems during machine learning inference benchmarks. This ensures accurate, reproducible, and realistic power measurements alongside performance metrics.

# Setup:

Components required:
- Power meter analyzer (Yokogawa)
- Director system (*director-system*)
- client System Under Test (*client-SUT*)

The following diagram provides a basic notion of the sequence:
https://github.com/mlcommons/power-dev/blob/master/ptd_client_server/doc/sequence.png

## Step 1: On both systems (director-system and client-SUT)

The client System Under Test (*client-SUT*) is connected to a power analyzer that measures power, voltage, and current.
The analyzer is controlled by a director system (*director-system*), which logs the data.

- Connect both systems to the same network.
-- Windows: Set network to *PRIVATE* and configure both systems with the same user and password.
-- Linux:   configure seamless ssh connection between both systems.

On both systems:

- Setup and update time into *UTC zone* (usually the ntp-server is on this time zone)
- Open console with *ADMIN priviledges*
- create directory structure: ./tools/power unzip the contents of power.zip in the created power directory.
```bash
   cd mlperf_v1.5 
   mkdir ./tools/power
   mv power.zip ./tools/power
   cd ./tools/power
   unzip power.zip
   cp ./tools/power/mlperf_client_power_efficiency.py .
```
You can use the following script to sync systems clock to the NTP server accordingly:
```bash
   /tools/power/setup/setup_timeSync.bat
```
- Install python and its requirements:
```bash
   pip install -r ./tools/power/setup/requirements.txt
```

##  Step 2: On *director-system* system

1. Download SPEC PTDaemon(r) Interface
   SPEC PTDaemon (can be downloaded from here after signing the EULA which can be requested by sending an email to support@mlcommons.org). 
   Once you have GitHub access to the MLCommons power repository you can place the SPEC PTDaemon tool into a selected directory [example: C:\ptd_spec\ptd-windows-x86.exe].
   https://docs.mlcommons.org/inference/power/
2. Edit following settings in ptd_client_server/server.mlperf_WT210.conf:
 - Path to PTDaemon binary [example: C:\ptd_spec\ptd-windows-x86.exe]
 - NTP server name or IP address
 - Server IP address and port

```bash
   $ cd tools/power/ptd_client_server/
   $ notepad server.mlperf_WT210.conf [EDIT parameters]
   $ server.py -c server.mlperf_WT210.conf
```

##  Step 3. On *client-SUT* system
Prepare MLPer-Client for the run to make sure required files are already downloaded.
If running on a battery power system, ensure it is full charged and connected t to the Analyzer.
Update client_collect_power.bat with desired configurations to run.

```bash
$ python mlperf_client_power_efficiency.py -c NVIDIA_Phi3.5_ORTGenAI-DML_GPU.json --ip 192.168.1.42 --ntp_server time.google.com -v --show_plot --stable_power
```

## FAQ

### 1. Supported Analyzers:
https://www.spec.org/power/docs/specpower-device_list/


### 2. How to test yokogawa device:
```bash
$ cd C:\ptd_spec\
$ ptd-windows-x86.exe -h
$ ptd-windows-x86.exe -g 8 1
```

## Documentation
- https://github.com/mlcommons/power-dev/tree/master
- https://github.com/mlcommons/ck/blob/master/docs/tutorials/mlperf-inference-power-measurement.md
- https://docs.mlcommons.org/inference/power/

## Energy Star Documentation
- https://www.energystar.gov/products/computers#bg
- https://www.energystar.gov/products/how-product-earns-energy-star-label
