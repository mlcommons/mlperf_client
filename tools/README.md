# Tools Helper

---

## Clang Format/Clang Tidy

Before running `run_tidy.bat` (for Windows) or `run_tidy.sh` (for macOS), ensure you have successfully built the application.

### Supported Options

Both `run_tidy.bat` and `run_tidy.sh` support several command-line options:

- `-b <build_dir>`:  This is the directory where your build files are located. By default, this is set to `.\build-ninja`.

- `-s <src_dir>`: This is the directory where your source files (`*.cpp`, `*.h`, etc.) are located. By default, this is set to `.\src`.

- `-a`: Analyze all files in the source directory. Without it, the script Formats/Analyze only recently modified files (git diff --name-only HEAD), optimizing for quicker runs during development.

- `-h`: Display the help message. This option prints out the usage information and exits the script.

### Running the Scripts

To run the scripts with the default options, simply navigate to the main project folder where the tools directory exists, and execute the appropriate script:

On Windows:

```cmd
.\tools\run_tidy.bat
```

On macOS:

```sh
./tools/run_tidy.sh
```

Note:
in order to generate the json compilation database, you can use the following command (add any additional flags you need):
```cmd
cmake -G Ninja -S . -B "build-ninja" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```
