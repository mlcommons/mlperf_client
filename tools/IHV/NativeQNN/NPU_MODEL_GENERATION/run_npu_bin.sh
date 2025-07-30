#!/bin/bash
read -p "Enter model name for npu bin generation ex: llama2, llama3.1 or phi3.5: " model_name

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

if [ "$model_name" == "llama2" ]; then
    example_1_success="example_1_success.stamp"

    if [ ! -f "$example_1_success" ]; then
        Run Python Script

        cp "run_example_1.py" "example-1/"

        cd "example-1"
        python run_example_1.py "$model_name"
        
        # Check if example-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "example-1 notebook python script executed successfully"
            cd "../"
            touch "$example_1_success"
        else
            echo "Error running example-1 notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Example 1 Notebook already executed"
    fi


    copy_of_onnx_success="copy_of_onnx_success.stamp"

    if [ ! -f "$copy_of_onnx_success" ]; then
        SOURCE_ONNX="example-1/output_dir/output/onnx"
        DESTINATION="example-2/host_linux/assets/models"
        SOURCE_TEST_VECTORS="example-1/output_dir/test_vectors"

        # Create Destination Directory
        mkdir -p "$DESTINATION"

        # Check if source onnx folder exists
        if [ ! -d "$SOURCE_ONNX" ]; then
            echo "Error: Source folder '$SOURCE_ONNX' does not exist."
            exit 1
        fi


        # Check if source test_vectors folder exists
        if [ ! -d "$SOURCE_TEST_VECTORS" ]; then
            echo "Error: Source folder '$SOURCE_TEST_VECTORS' does not exist."
            exit 1
        fi

        cp -r "$SOURCE_ONNX" "$DESTINATION"
        if [ $? -eq 0 ]; then
            echo "Folder copied successfully from '$SOURCE_ONNX' and '$SOURCE_TEST_VECTORS' to '$DESTINATION'."
        else
            echo "Error copying onnx folder."
            exit 1
        fi

        cp -r "$SOURCE_TEST_VECTORS" "$DESTINATION"
        if [ $? -eq 0 ]; then
            echo "Folder copied successfully from '$SOURCE_ONNX' and '$SOURCE_TEST_VECTORS' to '$DESTINATION'."
            touch "$copy_of_onnx_success"
        else
            echo "Error copying test vectors folder."
            exit 1
        fi
    else 
        echo "Copying of onnx and test_vectors is already done"
    fi

    copy_qairt_success="copy_of_qairt_sdk_success.stamp"
    SOURCE_QAIRT="qairt/2.35.3.250617/."
    DESTINATION_QAIRT="example-2/host_linux/assets/qnn/"
    if [ ! -f "$copy_qairt_success" ]; then
        # Check if source qairt sdk folder exists
        if [ ! -d "$SOURCE_QAIRT" ]; then
            echo "Error: Source folder '$SOURCE_QAIRT' does not exist."
            exit 1
        fi
        
        mkdir -p "$DESTINATION_QAIRT"
        if [ ! -d "$DESTINATION_QAIRT" ]; then
            echo "Error: Source folder '$DESTINATION_QAIRT' does not exist."
            exit 1
        fi
        
        cp -r "$SOURCE_QAIRT" "$DESTINATION_QAIRT"
        if [ $? -eq 0 ]; then
            echo "FOLDER copied successfully from '$SOURCE_QAIRT' to '$DESTINATION_QAIRT'."
            touch "$copy_qairt_success"
        else
            echo "Error copying qairt sdk folder."
            exit 1
        fi
    else
        echo "Copying of qairt sdk is already done"
    fi

    example_2_success="example_2_success.stamp"
    if [ ! -f "$example_2_success" ]; then
        Run Python Script

        cp "run_example_2.py" "example-2/host_linux/"
        if [ $? -eq 0 ]; then
            echo "run_example_2.py file copied successfully"
        else
            echo "Error copying run_example_2.py."
            exit 1
        fi

        chmod 777 -R "example-2/G2G/."
        #Verify if chmod was successfull
        if [ $? -eq 0 ]; then
            echo "Permission set to 777 for example-2/G2G/. folder."
        else
            echo "Error setting permissions for example-2/G2G/. folder."
            exit 1
        fi
        
        cd "example-2/host_linux/"

        python run_example_2.py "$model_name"
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "Step2 notebook python script executed successfully"
            cd "../../"
            touch "$example_2_success"
        else
            echo "Error running step2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Example2 notebook is already executed"
    fi

    copy_of_bins_success="copy_of_bins_success.stamp"
    if [ ! -f "$copy_of_bins_success" ]; then
        cp -r "example-2/host_linux/assets/artifacts/ar128-ar1-cl4096" "./"
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
        parent_dir="ar128_ar1_cl4096"
        cd "$parent_dir" || exit
        pwd

        for file in `ls .`; do
            echo "$file"    
        num=$(echo "$file" | cut -c 22)
            new_name="llama2_npu_${num}_of_4.bin"
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
        cp "htpBackendExtConfig.json" "ar128_ar1_cl4096/"
        if [ $? -eq 0 ]; then
            echo "Backend Extension is copied"
            touch "$copy_htp_backend_extension_success"
        else
            echo "Error in copying backend extension"
            exit 1
        fi
    fi

    zipped_command_success="zip_success.stamp"
    if [ ! -f "$zipped_command_success" ]; then
        cd "ar128_ar1_cl4096"
        apt install zip
        zip "llama2_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "copy llama2_npu.zip present in ar128_ar1_cl4096 folder"
        else
            echo "Error in zipping the folder content of ar128_ar1_cl4096"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_success"
    fi
elif [ "$model_name" == "llama3.1" ]; then
    echo "Llama3.1"
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
            new_name="llama3_npu_${num}_of_5.bin"
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
        cp "htpBackendExtConfig.json" "ar128_ar32_cl512_cl1024_cl2048_cl3072_cl4096/"
        if [ $? -eq 0 ]; then
            echo "Backend Extension is copied"
            touch "$copy_htp_backend_extension_success"
        else
            echo "Error in copying backend extension"
            exit 1
        fi
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
elif [ "$model_name" == "phi3.5" ]; then
    echo "Phi3.5"
    example1_success="example1_success.stamp"

    if [ ! -f "$example1_success" ]; then
        Run Python Script

        cp "run_example_1.py" "example1/"

        cd "example1"
        python run_example_1.py "$model_name"
        
        # Check if example-1 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "example1 notebook python script executed successfully"
            cd "../"
            touch "$example1_success"
        else
            echo "Error running example1 notebook python script."
            cd "../"
            exit 1
        fi
    else
        echo "Example 1 Notebook already executed"
    fi

    example2_success="example2_success.stamp"

    if [ ! -f "$example2_success" ]; then
        Run Python Script

        cp "run_example_2.py" "example2/host_linux/"

        cd "example2/host_linux/"
        python run_example_2.py "$model_name"
        
        # Check if example-2 script is executed successfully
        if [ $? -eq 0 ]; then
            echo "example2 notebook python script executed successfully"
            cd "../../"
            touch "$example2_success"
        else
            echo "Error running example2 notebook python script."
            cd "../../"
            exit 1
        fi
    else
        echo "Example 2 Notebook already executed"
    fi
    
    copy_of_bins_success="copy_of_bins_success.stamp"
    if [ ! -f "$copy_of_bins_success" ]; then
        cp -r "example2/host_linux/assets/ar1_ar128_cl512_cl1024_cl2048_cl3072_cl4096" "./"
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
        parent_dir="ar1_ar128_cl512_cl1024_cl2048_cl3072_cl4096"
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
        touch "$renaming_file_success"
    else 
        echo "Bins are already renamed"
    fi

    copy_htp_backend_extension_success="copy_htp_BE_success.stamp"
    if [ ! -f "$copy_htp_backend_extension_success" ]; then
        cp "htpBackendExtConfig.json" "ar1_ar128_cl512_cl1024_cl2048_cl3072_cl4096"
        if [ $? -eq 0 ]; then
            echo "Backend Extension is copied"
            touch "$copy_htp_backend_extension_success"
        else
            echo "Error in copying backend extension"
            exit 1
        fi
    fi

    zipped_command_success="zip_success.stamp"
    if [ ! -f "$zipped_command_success" ]; then
        cd "ar1_ar128_cl512_cl1024_cl2048_cl3072_cl4096"
        apt install zip
        zip "phi3_5_npu.zip" *
        if [ $? -eq 0 ]; then
            echo "copy phi3_5_npu.zip present in ar1_ar128_cl512_cl1024_cl2048_cl3072_cl4096"
        else
            echo "Error in zipping the folder content of ar1_ar128_cl512_cl1024_cl2048_cl3072_cl4096"
            cd ../
            exit 1
        fi
        cd ../
        touch "$zipped_command_success"
    fi
else
    echo "Model is not supported"
fi
