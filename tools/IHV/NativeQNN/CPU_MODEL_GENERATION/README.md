# Qualcomm's GenAI Conversion Package 

## Requirements for Running

<!-- markdown-link-check-disable-next-line -->
* [Qualcomm Package Manager 3](https://qpm.qualcomm.com/#/main/tools/details/QPM3)
* [QAIRT SDK](https://qpm.qualcomm.com/#/main/tools/details/qualcomm_neural_processing_sdk) (Version 2.35.3.250617)
* Python Version Requirement 3.10
* Windows machine capable of running powershell with latest python installed
* QAIRT SDK Location:\
**⚠️ Important:** The QAIRT SDK should be present in the same folder as the script or Specify its location using the **-qairt** parameter when running the script

## Steps to run the script

```shell
.\run_conversion.ps1 -qairt "C:\path\to\qairt_sdk" -model model_name
```
model_name can be any of them "llama2", "llama3" or "phi3.5" based on what you want to generate.
Replace "C:\path\to\qairt_sdk" with the actual path to your QAIRT SDK installation. If not provided, the script assumes QAIRT is located in the same directory as the script.
* Output Binary of CPU will be generated in current folder with name **"llama2_cpu.bin"**, **"llama3_cpu.bin"** or **"phi3_5_cpu.bin"**