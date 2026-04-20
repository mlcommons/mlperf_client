#!/bin/bash
read -p "Enter model name for npu bin generation ex: llama3.1, phi3.5 or phi4: " model_name

chmod_success="chmod_success.stamp"

if [ ! -f "$chmod_sucess" ]; then

    chmod 777 -R *

    # Verify if chmod was successfull
    if [ $? -eq 0 ]; then
        echo "Permission set to 777 for current folder."
        touch "$chmod_success"
    else
        echo "Error setting permissions."
        exit 1
    fi
else 
    echo "Chmod already executed"
fi

if [ "$model_name" == "llama3.1" ]; then
    echo "Llama3.1"
    step1_adascale_success="step1_adascale_success.stamp"

    if [ ! -f "$step1_adascale_success" ]; then

        cp "run_example_1_adascale.py" "Step-1/"

        cd "Step-1"
        python run_example_1_adascale.py "$model_name"
        
        # Check if Step-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step1 adascale notebook python script executed successfully"
            cd "../"
            touch "$step1_adascale_success"
        else
            echo "Error running step1 adascale notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Step 1 Adascale Notebook already executed"
    fi

    step1_success="step1_success.stamp"

    if [ ! -f "$step1_success" ]; then

        cp "run_example_1.py" "Step-1/"

        cd "Step-1"
        python run_example_1.py "$model_name"
        
        # Check if Step-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step1 notebook python script executed successfully"
            cd "../"
            touch "$step1_success"
        else
            echo "Error running step1 notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Step 1 Notebook already executed"
    fi

    step2_success="step2_success.stamp"

    if [ ! -f "$step2_success" ]; then

        cp "run_example_2.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "Step2 notebook python script executed successfully"
            cd "../../"
            touch "$step2_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Notebook already executed"
    fi
    
    copy_of_bins_success="copy_of_bins_success.stamp"
    if [ ! -f "$copy_of_bins_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096" "./"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_success"
        else
            echo "Error copying Bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_success="renaming_success.stamp"
    if [ ! -f "$renaming_file_success" ]; then
        parent_dir="ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
        cd "$parent_dir" || exit
        pwd
        

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 67)
            new_name="llama3_npu_${num}_of_6.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        if [ $? -eq 0 ]; then
            echo "Bins file renamed successfully"
        else
            echo "Error in renaming the bins"
            cd ../
            exit 1
        fi
        cd ../
        touch "$renaming_file_success"
    else 
        echo "Bins are already renamed"
    fi
   
    copy_htp_backend_extension_success="copy_htp_BE_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_success" ]; then
        echo '{ "devices": [ { "soc_id": 88, "dsp_arch": "v81", "cores": [ { "core_id": 0, "perf_profile": "burst", "rpc_control_latency": 100 } ], "pd_session": "unsigned" } ], "context": { "weight_sharing_enabled": true, "extended_udma": true }, "memory": { "mem_type": "shared_buffer" }, "groupContext": { "share_resources": false } }' > htpBackendExtconfig.json
        cp "htpBackendExtConfig.json" "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096/"
        if [ $? -eq 0 ]; then
            echo "Backend Extension is copied"
            touch "$copy_htp_backend_extension_success"
        else
            echo "Error in copying backend extension"
            exit 1
        fi
        rm htpBackendExtConfig.json
    fi

    zipped_command_success="zip_success.stamp"
    if [ ! -f "$zipped_command_success" ]; then
        cd "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
        apt install zip
        zip "llama3_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "copy llama3_npu.zip present in ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096 folder"
        else
            echo "Error in zipping the folder content of ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_success"
    fi

    # Running Notebook for SDX Elite
    step2_sdx_elite_success="step2_sdx_elite_success.stamp"

    if [ ! -f "$step2_sdx_elite_success" ]; then
        Run Python Script

        cp "run_example_2_sdx_elite.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2_sdx_elite.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step2 notebook python script executed successfully"
            cd "../../"
            touch "$step2_sdx_elite_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Notebook already executed"
    fi
    
    copy_of_bins_sdx_elite_success="copy_of_bins_sdx_elite_success.stamp"
    if [ ! -f "$copy_of_bins_sdx_elite_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096/." "./ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite/"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_sdx_elite_success"
        else
            echo "Error copying bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_sdx_elite_success="renaming_sdx_elite_success.stamp"
    if [ ! -f "$renaming_file_sdx_elite_success" ]; then
        parent_dir="ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        cd "$parent_dir" || exit
        pwd
        

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 67)
            new_name="llama3_npu_${num}_of_6.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        if [ $? -eq 0 ]; then
            echo "Bins file renamed successfully"
        else
            echo "Error in renaming the bins"
            cd ../
            exit 1
        fi
        cd ../
        touch "$renaming_file_sdx_elite_success"
    else 
        echo "Bins are already renamed"
    fi

    copy_htp_backend_extension_sdx_elite_success="copy_htp_BE_sdx_elite_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_success" ]; then
       echo '{ "devices": [ { "soc_id": 60, "dsp_arch": "v73", "cores": [ { "core_id": 0, "perf_profile": "burst", "rpc_control_latency": 100 } ], "pd_session": "unsigned" } ], "context": { "weight_sharing_enabled": true}, "memory": { "mem_type": "shared_buffer" }, "groupContext": { "share_resources": false } }' > htpBackendExtconfig.json
        
       cp "htpBackendExtConfig.json" "ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
       if [ $? -eq 0 ]; then
           echo "Backend Extension is copied"
           touch "$copy_htp_backend_extension_sdx_elite_success"
       else
           echo "Error in copying backend extension"
           exit 1
       fi
    fi

    zipped_command_sdx_elite_success="zip_sdx_elite_success.stamp"
    if [ ! -f "$zipped_command_sdx_elite_success" ]; then
        cd "ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        apt install zip
        zip "llama3_npu_hybrid.zip" *
        if [ $? -eq 0 ]; then
            echo "copy llama3_npu_hybrid.zip present in ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        else
            echo "Error in zipping the folder content of ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_sdx_elite_success"
    fi
elif [ "$model_name" == "phi3.5" ]; then
    echo "Phi3.5"
    step1_adascale_success="step1_adascale_success.stamp"

    if [ ! -f "$step1_adascale_success" ]; then

        cp "run_example_1_adascale.py" "Step-1/"

        cd "Step-1"
        python run_example_1_adascale.py "$model_name"
        
        # Check if Step-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step1 adascale notebook python script executed successfully"
            cd "../"
            touch "$step1_adascale_success"
        else
            echo "Error running step1 adascale notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Step 1 Adascale Notebook already executed"
    fi
    step1_success="step1_success.stamp"

    if [ ! -f "$step1_success" ]; then
        Run Python Script

        cp "run_example_1.py" "Step-1/"

        cd "Step-1"
        python run_example_1.py "$model_name"
        
        # Check if example-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step1 notebook python script executed successfully"
            cd "../"
            touch "$step1_success"
        else
            echo "Error running step1 notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Step 1 Notebook already executed"
    fi

    step2_success="step2_success.stamp"

    if [ ! -f "$step2_success" ]; then
        Run Python Script

        cp "run_example_2.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step2 notebook python script executed successfully"
            cd "../../"
            touch "$step2_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Notebook already executed"
    fi
    
    copy_of_bins__success="copy_of_bins_success.stamp"
    if [ ! -f "$copy_of_bins_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096" "./"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_sdx_elite_success"
        else
            echo "Error copying bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_success="renaming_success.stamp"
    if [ ! -f "$renaming_file_success" ]; then
        parent_dir="ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096"
        cd "$parent_dir" || exit
        pwd
        

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 66)
            new_name="phi3_5_npu_${num}_of_4.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        if [ $? -eq 0 ]; then
            echo "Bins file renamed successfully"
        else
            echo "Error in renaming the bins"
            cd ../
            exit 1
        fi
        cd ../
        touch "$renaming_file_sdx_elite_success"
    else 
        echo "Bins are already renamed"
    fi

    copy_htp_backend_extension_success="copy_htp_BE_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_success" ]; then
       echo '{ "devices": [ { "soc_id": 88, "dsp_arch": "v81", "cores": [ { "core_id": 0, "perf_profile": "burst", "rpc_control_latency": 100 } ], "pd_session": "unsigned" } ], "context": { "weight_sharing_enabled": true, "extended_udma": true, "reused_io_limit_mb": 100 }, "memory": { "mem_type": "shared_buffer" }, "groupContext": { "share_resources": false } }' > htpBackendExtConfig.json
       cp "htpBackendExtConfig.json" "ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
       if [ $? -eq 0 ]; then
           echo "Backend Extension is copied"
           touch "$copy_htp_backend_extension_success"
       else
           echo "Error in copying backend extension"
           exit 1
       fi
       rm htpBackendExtConfig.json
    fi

    zipped_command_success="zip_success.stamp"
    if [ ! -f "$zipped_command_success" ]; then
        cd "ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096"
        apt install zip
        zip "phi3_5_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "copy phi3_5_npu.zip present in ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096"
        else
            echo "Error in zipping the folder content of ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_success"
    fi

    step2_sdx_elite_success="step2_sdx_elite_success.stamp"

    if [ ! -f "$step2_sdx_elite_success" ]; then
        Run Python Script

        cp "run_example_2_sdx_elite.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2_sdx_elite.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step2 notebook python script executed successfully"
            cd "../../"
            touch "$step2_sdx_elite_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Notebook already executed"
    fi
    
    copy_of_bins_sdx_elite_success="copy_of_bins_sdx_elite_success.stamp"
    if [ ! -f "$copy_of_bins_sdx_elite_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096/." "./ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite/"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_sdx_elite_success"
        else
            echo "Error copying bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_sdx_elite_success="renaming_sdx_elite_success.stamp"
    if [ ! -f "$renaming_file_sdx_elite_success" ]; then
        parent_dir="ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        cd "$parent_dir" || exit
        pwd
        

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 66)
            new_name="phi3_5_npu_${num}_of_4.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        if [ $? -eq 0 ]; then
            echo "Bins file renamed successfully"
        else
            echo "Error in renaming the bins"
            cd ../
            exit 1
        fi
        cd ../
        touch "$renaming_file_sdx_elite_success"
    else 
        echo "Bins are already renamed"
    fi

    copy_htp_backend_extension_success="copy_htp_BE_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_success" ]; then
       echo '{ "devices": [ { "soc_id": 60, "dsp_arch": "v73", "cores": [ { "core_id": 0, "perf_profile": "burst", "rpc_control_latency": 100 } ], "pd_session": "unsigned" } ], "context": { "weight_sharing_enabled": true}, "memory": { "mem_type": "shared_buffer" }, "groupContext": { "share_resources": false } }' > htpBackendExtconfig.json
        
       cp "htpBackendExtConfig.json" "ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
       if [ $? -eq 0 ]; then
           echo "Backend Extension is copied"
           touch "$copy_htp_backend_extension_success"
       else
           echo "Error in copying backend extension"
           exit 1
       fi
       rm htpBackendExtConfig.json
    fi

    zipped_command_sdx_elite_success="zip_sdx_elite_success.stamp"
    if [ ! -f "$zipped_command_sdx_elite_success" ]; then
        cd "ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        apt install zip
        zip "phi3_5_npu_hybrid.zip" *
        if [ $? -eq 0 ]; then
            echo "copy phi3_5_npu_hybrid.zip present in ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        else
            echo "Error in zipping the folder content of ar128_ar1_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_sdx_elite_success"
    fi
elif [ "$model_name" == "phi4" ]; then
    echo "Phi4"
    step1_success="step1_success.stamp"

    if [ ! -f "$step1_success" ]; then
        Run Python Script

        cp "run_example_1.py" "Step-1/"

        cd "Step-1"
        python run_example_1.py "$model_name"
        
        # Check if example-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step1 notebook python script executed successfully"
            cd "../"
            touch "$step1_success"
        else
            echo "Error running example1 notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Step 1 Notebook already executed"
    fi
    
    step2_success="step2_success.stamp"

    if [ ! -f "$step2_success" ]; then
        Run Python Script

        cp "run_example_2.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step2 notebook python script executed successfully"
            cd "../../"
            touch "$step2_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Notebook already executed"
    fi
    
    copy_of_bins_success="copy_of_bins_success.stamp"
    if [ ! -f "$copy_of_bins_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096" "./"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_success"
        else
            echo "Error copying bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_success="renaming_success.stamp"
    if [ ! -f "$renaming_file_success" ]; then
        parent_dir="ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
        cd "$parent_dir" || exit
        pwd
        

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 67)
            new_name="phi4_npu_${num}_of_9.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        if [ $? -eq 0 ]; then
            echo "Bins file renamed successfully"
        else
            echo "Error in renaming the bins"
            cd ../
            exit 1
        fi
        cd ../
        touch "$renaming_file_success"
    else 
        echo "Bins are already renamed"
    fi

    copy_htp_backend_extension_success="copy_htp_BE_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_success" ]; then
       echo '{ "devices": [ { "soc_id": 88, "dsp_arch": "v81", "cores": [ { "core_id": 0, "perf_profile": "burst", "rpc_control_latency": 100 } ], "pd_session": "unsigned" } ], "context": { "weight_sharing_enabled": true, "extended_udma": true }, "memory": { "mem_type": "shared_buffer" }, "groupContext": { "share_resources": false } }' > htpBackendExtconfig.json
       cp "htpBackendExtConfig.json" "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
       if [ $? -eq 0 ]; then
           echo "Backend Extension is copied"
           touch "$copy_htp_backend_extension_success"
       else
           echo "Error in copying backend extension"
           exit 1
       fi
       rm htpBackendExtConfig.json
    fi

    zipped_command_success="zip_success.stamp"
    if [ ! -f "$zipped_command_success" ]; then
        cd "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
        apt install zip
        zip "phi4_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "copy phi4_npu.zip present in ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
        else
            echo "Error in zipping the folder content of ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_success"
    fi

    step2_sdx_elite_success="step2_sdx_elite_success.stamp"

    if [ ! -f "$step2_sdx_elite_success" ]; then
        Run Python Script

        cp "run_example_2_sdx_elite.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2_sdx_elite.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "step2 notebook python script executed successfully"
            cd "../../"
            touch "$step2_sdx_elite_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Notebook already executed"
    fi
    
    copy_of_bins_sdx_elite_success="copy_of_bins_sdx_elite_success.stamp"
    if [ ! -f "$copy_of_bins_sdx_elite_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096/." "./ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite/"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_sdx_elite_success"
        else
            echo "Error copying bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_sdx_elite_success="renaming_sdx_elite_success.stamp"
    if [ ! -f "$renaming_file_sdx_elite_success" ]; then
        parent_dir="ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        cd "$parent_dir" || exit
        pwd
        

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 67)
            new_name="phi4_npu_${num}_of_9.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        if [ $? -eq 0 ]; then
            echo "Bins file renamed successfully"
        else
            echo "Error in renaming the bins"
            cd ../
            exit 1
        fi
        cd ../
        touch "$renaming_file_sdx_elite_success"
    else 
        echo "Bins are already renamed"
    fi

    copy_htp_backend_extension_sdx_elite_success="copy_htp_BE_sdx_elite_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_sdx_elite_success" ]; then
       echo '{ "devices": [ { "soc_id": 60, "dsp_arch": "v73", "cores": [ { "core_id": 0, "perf_profile": "burst", "rpc_control_latency": 100 } ], "pd_session": "unsigned" } ], "context": { "weight_sharing_enabled": true}, "memory": { "mem_type": "shared_buffer" }, "groupContext": { "share_resources": false } }' > htpBackendExtconfig.json
       
       cp "htpBackendExtConfig.json" "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
       if [ $? -eq 0 ]; then
           echo "Backend Extension is copied"
           touch "$copy_htp_backend_extension_sdx_elite_success"
       else
           echo "Error in copying backend extension"
           exit 1
       fi
    fi

    zipped_command_sdx_elite_success="zip_sdx_elite_success.stamp"
    if [ ! -f "$zipped_command_sdx_elite_success" ]; then
        cd "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        apt install zip
        zip "phi4_npu_hybrid.zip" *
        if [ $? -eq 0 ]; then
            echo "copy phi4_npu_hybrid.zip present in ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
        else
            echo "Error in zipping the folder content of ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_sdx_elite"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_sdx_elite_success"
    fi
else
    echo "Model is not supported"
fi
