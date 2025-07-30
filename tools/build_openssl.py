import os
import subprocess
import platform


def find_vs_dev_cmd():
    potential_paths = [
        r"D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
    ]

    for path in potential_paths:
        if os.path.exists(path):
            return path

    print("Unable to find VsDevCmd.bat automatically.")
    user_input_path = input("Please enter the full path to VsDevCmd.bat: ").strip()
    if os.path.exists(user_input_path):
        return user_input_path
    else:
        print("The provided path does not exist. Please check the path and try again.")
        return None


def build_openssl_for_windows(openssl_src_dir, openssl_target_dir):
    vs_dev_cmd_path = find_vs_dev_cmd()
    if not vs_dev_cmd_path:
        print("Visual Studio Developer Command Prompt not found. Please ensure Visual Studio is installed.")
        return

    if not os.path.exists(os.path.join(openssl_src_dir, '.git')):
        subprocess.run(["git", "clone", "https://github.com/openssl/openssl.git", openssl_src_dir], check=True)

    os.chdir(openssl_src_dir)

    subprocess.run(["git", "checkout", "openssl-3.0.12"], check=True)

    subprocess.run(["git", "submodule", "init"], check=True)
    subprocess.run(["git", "submodule", "update"], check=True)

    # Requires perl and nasm which can be installed using e.g. Chocolatey
    if platform.machine().lower() == 'arm64':
        openssl_platform = "VC-WIN64-ARM"
    else:
        openssl_platform = "VC-WIN64A"

    configure_command = f"perl Configure {openssl_platform} no-shared --prefix=\"{openssl_target_dir}\" --openssldir=\"{openssl_target_dir}\""
    build_command = f"\"{vs_dev_cmd_path}\" && {configure_command} && nmake && nmake install"
    subprocess.run(build_command, check=True, shell=True)

    print("OpenSSL has been built successfully.")

def build_openssl_for_darwin(openssl_src_dir, openssl_target_dir, ios_target):
    if not os.path.exists(os.path.join(openssl_src_dir, '.git')):
        subprocess.run(["git", "clone", "https://github.com/openssl/openssl.git", openssl_src_dir], check=True)

    os.chdir(openssl_src_dir)

    subprocess.run(["git", "checkout", "openssl-3.0.12"], check=True)

    subprocess.run(["git", "submodule", "init"], check=True)
    subprocess.run(["git", "submodule", "update"], check=True)

    if ios_target:
        openssl_platform = 'ios64-cross'
        ios_sdk_path = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path']).decode().strip()
        configure_command_extra_args = f" -isysroot \"{ios_sdk_path}\""
    else:
        if platform.processor() == 'arm':
            openssl_platform = 'darwin64-arm64-cc'
        else:
            openssl_platform = 'darwin64-x86_64-cc'
        configure_command_extra_args = f""
    configure_command = f"./Configure {openssl_platform} no-shared --prefix=\"{openssl_target_dir}\" --openssldir=\"{openssl_target_dir}\""
    configure_command += configure_command_extra_args

    build_command = f"{configure_command} && make && make install"
    subprocess.run(build_command, check=True, shell=True)

    #print("OpenSSL has been built successfully.")

    


if __name__ == '__main__':
    import platform
    import os
    import argparse

    # Create a parser
    parser = argparse.ArgumentParser(description="Get some hyperparameters.")

    parser.add_argument("source_dir", help="Path to the OpenSSL directory")
    parser.add_argument("--ios", action="store_true", help="Build for iOS")

    # Get our arguments from the parser
    args = parser.parse_args()

    SOURCE_DIR = os.path.join(args.source_dir, "src")
    
    if platform.system() == "Windows":
        TARGET_DIR = os.path.join(args.source_dir, "Windows", "MSVC")
    elif platform.system() == "Darwin":
        TARGET_DIR = os.path.join(args.source_dir, "iOS" if args.ios else "macOS", "ARM")    
    else:
        print("Unsupported platform.")

    # Get the system's operating system
    os_system = platform.system()

    if os_system == "Windows":
        print("Windows OS detected.")
        build_openssl_for_windows(SOURCE_DIR, TARGET_DIR)
    elif os_system == "Darwin":
        build_openssl_for_darwin(SOURCE_DIR, TARGET_DIR, args.ios)
    else:
        print("Build is supported for macOS/Windows only!")
