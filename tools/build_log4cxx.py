import os
import platform
import subprocess
import argparse
import shutil


def build_log4cxx(log4cxx_root_dir, retry_on_fail):
    log4cxx_src_dir = os.path.join(log4cxx_root_dir, "src")
    log4cxx_build_dir = os.path.join(log4cxx_root_dir, "build")

    if not os.path.exists(log4cxx_build_dir):
        os.makedirs(log4cxx_build_dir)

    if platform.system() == "Windows":
        generator = "Visual Studio 17 2022"
        install_dir = os.path.join(log4cxx_root_dir, "Windows", "MSVC")
    elif platform.system() == "Darwin":
        generator = "Xcode"
        install_dir = os.path.join(log4cxx_root_dir, "macOS")
    else:
        print("Unsupported platform.")
        return

    if not os.path.exists(install_dir):
        os.makedirs(install_dir)

    cmake_command = ["cmake", "-G", generator, "-S", log4cxx_src_dir, "-B", log4cxx_build_dir, "-DCMAKE_INSTALL_PREFIX=" + install_dir]

    try:
        subprocess.run(cmake_command, check=True)

        # Perform build
        subprocess.run(["cmake", "--build", ".", "--config", "Release"], check=True, cwd=log4cxx_build_dir)

        # Perform installation
        subprocess.run(["cmake", "--install", ".", "--config", "Release"], check=True, cwd=log4cxx_build_dir)

        print(f"\nLog4Cxx was successfully built and installed to '{install_dir}'.")

    except subprocess.CalledProcessError:
        print("\nBuild failed. Deleting x64 folder and retrying...")
        
        x64_folder = os.path.join(log4cxx_build_dir, "x64")
        if os.path.exists(x64_folder):
            shutil.rmtree(x64_folder)

        if retry_on_fail:
            build_log4cxx(log4cxx_root_dir, False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Build and install Log4Cxx.")
    parser.add_argument("source_dir", help="Path to the Log4Cxx directory")
    args = parser.parse_args()

    build_log4cxx(args.source_dir, True)
