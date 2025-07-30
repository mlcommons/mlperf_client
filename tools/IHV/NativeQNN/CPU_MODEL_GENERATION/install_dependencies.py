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

import os, subprocess, venv

def create_virtual_environment(env_name):
    """Creating Virtual Environmemt"""
    venv.create(env_name, with_pip=True)
    print(f"Virtual environment '{env_name}' created successfully")

def run_in_virtual_environment(env_name, commmand):
    """Run a command in the virtual environment"""
    activate_command = ""
    if(os.name == 'nt'):
        activate_script = os.path.join(env_name, 'Scripts', 'activate')
        activate_command = f"{activate_script} && {commmand}"
        print("Activate Command: " + activate_command)
    else:
        print(f"This Operating System is not supported currently")
        sys.exit(1)
    
    subprocess.run(activate_command, shell=True, check=True)

def install_packages_in_venv(env_name):
    """Install required packages"""
    packages = ["requests", "tqdm", "numpy", "sentencepiece", "cmake", "argparse"]
    run_in_virtual_environment(env_name, f"pip install {' '.join(packages)}")

if __name__ == "__main__":
    env_name = "bin_generation"
    create_virtual_environment(env_name)
    install_packages_in_venv(env_name)