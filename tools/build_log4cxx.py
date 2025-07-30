import os
import platform
import subprocess
import argparse

def build_log4cxx(log4cxx_root_dir, ios_target):
    log4cxx_src_dir = os.path.join(log4cxx_root_dir, "src")
    log4cxx_build_dir = os.path.join(log4cxx_root_dir, "build")

    if not os.path.exists(log4cxx_build_dir):
        os.makedirs(log4cxx_build_dir)

    if platform.system() == "Windows":
        generator = "Visual Studio 17 2022"
        install_dir = os.path.join(log4cxx_root_dir, "Windows", "MSVC")
    elif platform.system() == "Darwin":
        generator = "Xcode"
        install_dir = os.path.join(log4cxx_root_dir, "iOS" if args.ios else "macOS", "ARM")    
    else:
        print("Unsupported platform.")
        return

    if not os.path.exists(install_dir):
        os.makedirs(install_dir)

    cmake_command = ["cmake", "-G", generator, "-S", log4cxx_src_dir, "-B", log4cxx_build_dir, "-DCMAKE_INSTALL_PREFIX=" + install_dir]

    if platform.system() == "Darwin" and ios_target:
        ios_specific_flags = [
            "-DTARGET_IOS=ON"
        ]
        cmake_command += ios_specific_flags

    try:
        subprocess.run(cmake_command, check=True)

        # Perform build
        subprocess.run(["cmake", "--build", ".", "--config", "Release"], check=True, cwd=log4cxx_build_dir)

        # Perform installation
        subprocess.run(["cmake", "--install", ".", "--config", "Release"], check=True, cwd=log4cxx_build_dir)

        print(f"\nLog4Cxx was successfully built and installed to '{install_dir}'.")

    except subprocess.CalledProcessError:
        print("\nBuild failed. Please check the output for details.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Build and install Log4Cxx.")
    parser.add_argument("source_dir", help="Path to the Log4Cxx directory")
    parser.add_argument("--ios", action="store_true", help="Build for iOS")
    args = parser.parse_args()

    build_log4cxx(args.source_dir, args.ios)