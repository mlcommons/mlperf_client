#!/bin/bash
read -p "Enter model name for npu bin generation ex: llama3.1, phi4mini or phi4: " model_name

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

    # Agentic binary generation (extended CL_LIST)
    step2_agentic_success="step2_agentic_success.stamp"

    if [ ! -f "$step2_agentic_success" ]; then

        cp "run_example_2_agentic.py" "Step-2/host_linux/"

        cd "Step-2/host_linux/"
        python run_example_2_agentic.py "$model_name"

        if [ $? -eq 0 ]; then
            echo "Step2 agentic notebook python script executed successfully"
            cd "../../"
            touch "$step2_agentic_success"
        else
            echo "Error running step2 agentic notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Agentic Notebook already executed"
    fi

    copy_of_bins_agentic_success="copy_of_bins_agentic_success.stamp"
    if [ ! -f "$copy_of_bins_agentic_success" ]; then
        cp -r "Step-2/host_linux/assets/ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_cl8192_cl10240_cl12288" "./"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_agentic_success"
        else
            echo "Error copying Bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_agentic_success="renaming_agentic_success.stamp"
    if [ ! -f "$renaming_agentic_success" ]; then
        parent_dir="ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_cl8192_cl10240_cl12288"
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
        touch "$renaming_agentic_success"
    else
        echo "Bins are already renamed"
    fi

    zipped_agentic_success="zip_agentic_success.stamp"
    if [ ! -f "$zipped_agentic_success" ]; then
        mkdir -p "llama3_agentic"
        cd "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096_cl8192_cl10240_cl12288"
        apt install zip
        zip "../llama3_agentic/llama3_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "llama3_npu.zip created in llama3_agentic folder"
        else
            echo "Error in zipping the folder content"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_agentic_success"
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
elif [ "$model_name" == "phi4mini" ]; then
    echo "Phi4mini"
    # Notebook target codenames map to: SDX2 Elite (soc_id > 80) and SDX Elite

    step1_adascale_success="step1_adascale_success.stamp"

    if [ ! -f "$step1_adascale_success" ]; then

        cp "run_example_1_adascale.py" "example1/"

        cd "example1"
        python run_example_1_adascale.py "$model_name"

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

        cp "run_example_1.py" "example1/"

        cd "example1"
        python run_example_1.py "$model_name"

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

    # NPU normal -> SDX2 Elite
    step2_success="step2_success.stamp"

    if [ ! -f "$step2_success" ]; then

        cp "run_example_2.py" "example2/host_linux/"

        cd "example2/host_linux/"
        python run_example_2.py "$model_name"

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
        bin_dir=$(ls -d example2/host_linux/assets_phi4_mini_instruct_*_multigraph/*_cl4096_glymur 2>/dev/null | head -1)
        if [ -z "$bin_dir" ]; then
            echo "Error: could not find SDX2 Elite *_cl4096_glymur output directory"
            exit 1
        fi
        rm -rf "./phi4mini_npu_assets"
        mkdir -p "./phi4mini_npu_assets"
        cp "$bin_dir"/*.serialized.bin "./phi4mini_npu_assets/"
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
        cd "phi4mini_npu_assets" || exit
        for file in *.serialized.bin; do
            num=$(echo "$file" | sed -n 's/.*_\([0-9]\+\)_of_6_.*/\1/p')
            new_name="phi4_mini_npu_${num}_of_6.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        cd ../
        touch "$renaming_file_success"
    else
        echo "Bins are already renamed"
    fi

    zipped_command_success="zip_success.stamp"
    if [ ! -f "$zipped_command_success" ]; then
        cd "phi4mini_npu_assets"
        apt install zip
        zip "../phi4mini_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "phi4mini_npu.zip created successfully"
        else
            echo "Error in zipping phi4mini_npu_assets"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_success"
    fi

    # SDX Elite
    step2_sdx_elite_success="step2_sdx_elite_success.stamp"

    if [ ! -f "$step2_sdx_elite_success" ]; then

        cp "run_example_2_sdx_elite.py" "example2/host_linux/"

        cd "example2/host_linux/"
        python run_example_2_sdx_elite.py "$model_name"

        if [ $? -eq 0 ]; then
            echo "step2 sdx elite notebook python script executed successfully"
            cd "../../"
            touch "$step2_sdx_elite_success"
        else
            echo "Error running step2 sdx elite notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 SDX Elite Notebook already executed"
    fi

    copy_of_bins_sdx_elite_success="copy_of_bins_sdx_elite_success.stamp"
    if [ ! -f "$copy_of_bins_sdx_elite_success" ]; then
        bin_dir=$(ls -d example2/host_linux/assets_phi4_mini_instruct_*_multigraph/*_cl4096_hamoa 2>/dev/null | head -1)
        if [ -z "$bin_dir" ]; then
            echo "Error: could not find SDX Elite *_cl4096_hamoa output directory"
            exit 1
        fi
        rm -rf "./phi4mini_npu_hybrid_assets"
        mkdir -p "./phi4mini_npu_hybrid_assets"
        cp "$bin_dir"/*.serialized.bin "./phi4mini_npu_hybrid_assets/"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_sdx_elite_success"
        else
            echo "Error copying Bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_file_sdx_elite_success="renaming_sdx_elite_success.stamp"
    if [ ! -f "$renaming_file_sdx_elite_success" ]; then
        cd "phi4mini_npu_hybrid_assets" || exit
        for file in *.serialized.bin; do
            num=$(echo "$file" | sed -n 's/.*_\([0-9]\+\)_of_6_.*/\1/p')
            new_name="phi4_mini_npu_${num}_of_6.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        cd ../
        touch "$renaming_file_sdx_elite_success"
    else
        echo "Bins are already renamed"
    fi

    zipped_command_sdx_elite_success="zip_sdx_elite_success.stamp"
    if [ ! -f "$zipped_command_sdx_elite_success" ]; then
        cd "phi4mini_npu_hybrid_assets"
        apt install zip
        zip "../phi4mini_npu_hybrid.zip" *
        if [ $? -eq 0 ]; then
            echo "phi4mini_npu_hybrid.zip created successfully"
        else
            echo "Error in zipping phi4mini_npu_hybrid_assets"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_sdx_elite_success"
    fi

    # Agentic binary generation (extended CL_LIST)
    step2_agentic_success="step2_agentic_success.stamp"

    if [ ! -f "$step2_agentic_success" ]; then

        cp "run_example_2_agentic.py" "example2/host_linux/"

        cd "example2/host_linux/"
        python run_example_2_agentic.py "$model_name"

        if [ $? -eq 0 ]; then
            echo "Step2 agentic notebook python script executed successfully"
            cd "../../"
            touch "$step2_agentic_success"
        else
            echo "Error running step2 agentic notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Step 2 Agentic Notebook already executed"
    fi

    copy_of_bins_agentic_success="copy_of_bins_agentic_success.stamp"
    if [ ! -f "$copy_of_bins_agentic_success" ]; then
        bin_dir=$(ls -d example2/host_linux/assets_phi4_mini_instruct_*_multigraph/*_cl12288_glymur 2>/dev/null | head -1)
        if [ -z "$bin_dir" ]; then
            echo "Error: could not find SDX2 Elite agentic *_cl12288_glymur output directory"
            exit 1
        fi
        rm -rf "./phi4mini_agentic_assets"
        mkdir -p "./phi4mini_agentic_assets"
        cp "$bin_dir"/*.serialized.bin "./phi4mini_agentic_assets/"
        if [ $? -eq 0 ]; then
            echo "Bins file copied successfully to current folder"
            touch "$copy_of_bins_agentic_success"
        else
            echo "Error copying Bins."
            exit 1
        fi
    else
        echo "Bins are already copied to current folder successfully"
    fi

    renaming_agentic_success="renaming_agentic_success.stamp"
    if [ ! -f "$renaming_agentic_success" ]; then
        cd "phi4mini_agentic_assets" || exit
        for file in *.serialized.bin; do
            num=$(echo "$file" | sed -n 's/.*_\([0-9]\+\)_of_6_.*/\1/p')
            new_name="phi4_mini_npu_${num}_of_6.bin"
            mv "$file" "$new_name"
            echo "Renamed $file to $new_name"
        done
        cd ../
        touch "$renaming_agentic_success"
    else
        echo "Bins are already renamed"
    fi

    zipped_agentic_success="zip_agentic_success.stamp"
    if [ ! -f "$zipped_agentic_success" ]; then
        mkdir -p "phi4mini_agentic"
        cd "phi4mini_agentic_assets"
        apt install zip
        zip "../phi4mini_agentic/phi4mini_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "phi4mini_npu.zip created in phi4mini_agentic folder"
        else
            echo "Error in zipping phi4mini_agentic_assets"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_agentic_success"
    fi
else
    echo "Model is not supported"
fi
