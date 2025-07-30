''' Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
=============================================================================='''

import sys, os, subprocess, argparse

def download_with_progress(url, destination):
    import requests
    from tqdm import tqdm
    """Download file from url with progress bar"""
    response = requests.get(url, stream=True)
    total_size = int(response.headers.get('content-length', 0))
    block_size = 1024

    tqdm_bar = tqdm(total=total_size, unit='iB', unit_scale=True, desc=destination)

    with open(destination, 'wb') as file:
        for data in response.iter_content(block_size):
            tqdm_bar.update(len(data))
            file.write(data)
    tqdm_bar.close()

    if(total_size != 0 and tqdm_bar.n != total_size):
        print("Error: Something went wrong in fetching rust")
        sys.exit(1)

def run_process(env_name, qairt_path='qairt', model_name="llama2"):
    """Installing Rust"""
    try:
        if not os.path.exists(qairt_path):
            print(f"Qualcomm's QAIRT SDK is not present at {qairt_path}, please ensure that it is present.")
            sys.exit(1)
        rustup_init_url = "https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe"
        rustup_init_path = "rustup-init.exe"

        print("Downloading rustup-init.exe")
        download_with_progress(rustup_init_url, rustup_init_path)

        env_path = os.path.abspath(env_name)
        env_bin_path = os.path.join(env_path, "Scripts") if os.name == 'nt' else os.path.join(env_path, "bin")

        env = os.environ.copy()
        env["CARGO_HOME"] = os.path.join(env_path, ".cargo")
        env["RUSTUP_HOME"] = os.path.join(env_path, ".rustup")
        qairt_path = os.path.abspath(qairt_path)
        qairt_version_path = os.listdir(qairt_path)[0]
        env["LD_LIBRARY_PATH"] = os.path.join(env_path, qairt_path, qairt_version_path, "lib")
        env["QAIRT_SDK_ROOT"] = os.path.join(env_path, qairt_path, qairt_version_path)
        env["QNN_SDK_ROOT"] = os.path.join(env_path, qairt_path, qairt_version_path)
        env["PYTHONPATH"] = os.path.join(env_path, qairt_path, qairt_version_path, "lib", "python")
        path_composer_dll = os.path.join(env["QAIRT_SDK_ROOT"], "lib", "x86_64-windows-msvc")
        env["PATH"] = f"{env_bin_path};{os.path.join(env['CARGO_HOME'], 'bin')};{path_composer_dll};{env['PATH']}"
        print("All the Environmet paths are set successfully...")
        
        subprocess.run([rustup_init_path, "-y"], check=True, env=env)
        print("Rust has been installed successfully in virtual environment...")
        
        print("Clonning the git repository...")
        if model_name == "llama2":
            if(os.path.exists("Llama-2-7b-chat-hf")):
                print("Repository already exists...")
            else:
                git_clone_command = f"git clone https://huggingface.co/meta-llama/Llama-2-7b-chat-hf"
                subprocess.run(git_clone_command, shell=True, check=True)
                print("Git repo is clonned successfully...")
            model_file_path = os.path.abspath("Llama-2-7b-chat-hf") 
            out_file_path = os.path.join(os.path.abspath(""), "llama2_cpu.bin")
        elif model_name == "llama3":
            if(os.path.exists("Llama-3.1-8B-Instruct")):
                print("Repository already exists...")
            else:
                git_clone_command = f"git clone https://huggingface.co/meta-llama/Llama-3.1-8B-Instruct"
                subprocess.run(git_clone_command, shell=True, check=True)
                print("Git repo is clonned successfully...")
            model_file_path = os.path.abspath("Llama-3.1-8B-Instruct") 
            out_file_path = os.path.join(os.path.abspath(""), "llama3_cpu.bin")
        elif model_name == "phi3.5":
            if(os.path.exists("Phi-3.5-mini-instruct")):
                print("Repository already exists...")
            else:
                git_clone_command = f"git clone https://huggingface.co/microsoft/Phi-3.5-mini-instruct"
                subprocess.run(git_clone_command, shell=True, check=True)
                print("Git repo is clonned successfully...")
            model_file_path = os.path.abspath("Phi-3.5-mini-instruct") 
            out_file_path = os.path.join(os.path.abspath(""), "phi3_5_cpu.bin")
        

        path_composer = os.path.join(env["QAIRT_SDK_ROOT"], "bin", "x86_64-windows-msvc", "qnn-genai-transformer-composer")
        
        
        
        print("Running Bin Generation...")
        command = f"python {path_composer} --quantize Z8 --outfile {out_file_path} --model {model_file_path}" 
        subprocess.run(command, shell=True, check=True, env=env)
        print("Bin Generation is completed successfully for model " + model_name + "....")
        
    except subprocess.CalledProcessError as e:
        print(f"An error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Run package script.')
    parser.add_argument('--qairt', default='qairt', help='Path to QAIRT SDK (default: qairt)')
    parser.add_argument('--model', default='llama2', help='Path to Model (default: llama2)')
    args = parser.parse_args()
    print(args.model)

    run_process("bin_generation", qairt_path=args.qairt, model_name=args.model)