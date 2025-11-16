@echo off
setlocal enabledelayedexpansion

:: if using AMR: 143.183.33.10 corp.intel.com
set SERVER_IP=192.168.1.5
set NTP_SERVER=time.google.com
set OUTDIR=OUTDIR
set CMD_BASELINE=C:\work\mlperf-power\runners\run-mlperf
set FOLDER_EXECUTABLE=C:\work\mlperf-client-1.0.0\
set LOADGEN_OTHER_FILES=debug.log results.json


set CONFIG_LIST=Llama2_NVIDIA_ORTGenAI-DML_GPU_test

::INTEL
::Llama2_Intel_ORTGenAI-DML_GPU ^
::Llama3.1_Intel_ORTGenAI-DML_GPU ^
::Phi3.5_Intel_ORTGenAI-DML_GPU ^
::Llama2_Intel_NativeOpenVINO_GPU_Default ^
::Llama3.1_Intel_NativeOpenVINO_GPU_Default ^
::Phi3.5_Intel_NativeOpenVINO_GPU_Default ^
::Llama2_Intel_NativeOpenVINO_NPU_Default ^
::Llama3.1_Intel_NativeOpenVINO_NPU_Default ^
::Phi3.5_Intel_NativeOpenVINO_NPU_Default

::AMD
::Llama2_AMD_ORTGenAI-DML_GPU ^
::Llama3.1_AMD_ORTGenAI-DML_GPU ^
::Phi3.5_AMD_ORTGenAI-DML_GPU ^
::Llama2_AMD_OrtGenAI-RyzenAI_NPU-GPU ^
::Llama3.1_AMD_OrtGenAI-RyzenAI_NPU-GPU ^
::Phi3.5_AMD_OrtGenAI-RyzenAI_NPU-GPU

::NVIDIA
::Llama2_NVIDIA_ORTGenAI-DML_GPU ^
::Llama3.1_NVIDIA_ORTGenAI-DML_GPU ^
::Phi3.5_NVIDIA_ORTGenAI-DML_GPU

::Qualcomm
::Llama2_Qualcomm_NativeQNN_NPU-CPU ^
::Llama3.1_Qualcomm_ORTGenAI-DML_GPU ^
::Phi3.5_Qualcomm_ORTGenAI-DML_GPU

for %%c in (%CONFIG_LIST%) do (
    echo ============================
    echo Processing %%c
    
    echo %%c | findstr /i "llama2" >nul
    if !errorlevel! == 0 (
        set "LOADGEN_LOG_FILE=llama2_executor.log"
    ) else (
        echo %%c | findstr /i "llama3" >nul
        if !errorlevel! == 0 (
            set "LOADGEN_LOG_FILE=llama3_executor.log"
        ) else (
            echo %%c | findstr /i "phi3" >nul
            if !errorlevel! == 0 (
                set "LOADGEN_LOG_FILE=phi3_5_executor.log"
            )
        )
    )

    set "LABEL=%%c"
    set "CMD=%CMD_BASELINE%_%%c.bat"
    set "OUT_FOLDER=%FOLDER_EXECUTABLE%\%%c"

    echo $ client.py -l !LABEL! -g !LOADGEN_LOG_FILE! -w !CMD! -L !OUT_FOLDER! -a %SERVER_IP% -n %NTP_SERVER% -o %OUTDIR% -j !LOADGEN_OTHER_FILES! 
    client.py -l !LABEL! -g !LOADGEN_LOG_FILE! -w !CMD! -L !OUT_FOLDER! -a %SERVER_IP% -n %NTP_SERVER% -o %OUTDIR% -j !LOADGEN_OTHER_FILES! 
)

::client.py -a 192.168.1.16  -n time.google.com -w C:\work\mlperf-power\run_mlperf-main-power.bat -L C:\work\mlperf-client-1.0.0\Llama2_INTEL_ORTGenAI-DML_GPU -g llama2_executor.log -o OUTDIR -l llama2
::client.py -a 192.168.1.16  -n time.google.com -w C:\work\mlperf-power\run_mlperf-main-power.bat -L C:\work\mlperf-client-1.0.0\Llama3.1_INTEL_ORTGenAI-DML_GPU -g llama3_executor.log -o OUTDIR -l llama3
::client.py -a 192.168.1.16  -n time.google.com -w C:\work\mlperf-power\run_mlperf-main-power.bat -L C:\work\mlperf-client-1.0.0\Phi3.5_INTEL_ORTGenAI-DML_GPU -g phi3_5_executor.log -o OUTDIR -l phi3
