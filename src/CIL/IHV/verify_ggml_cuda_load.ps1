param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath
)

if (-not (Test-Path -LiteralPath $DllPath -PathType Leaf)) {
    throw "GGML CUDA provider was not staged: $DllPath"
}
$DllPath = [System.IO.Path]::GetFullPath($DllPath)

# Loading an ARM64 CUDA provider needs a native ARM64 process and the NVIDIA driver.
if ($env:PROCESSOR_ARCHITECTURE -ne 'ARM64') {
    Write-Host "Skipping GGML CUDA load test: host process is $env:PROCESSOR_ARCHITECTURE, not native ARM64."
    exit 0
}
if (-not (Test-Path "$env:SystemRoot\System32\nvcuda.dll")) {
    Write-Host "Skipping GGML CUDA load test: NVIDIA driver (nvcuda.dll) is not installed."
    exit 0
}

$savedLib = $env:LIB
Remove-Item Env:LIB -ErrorAction SilentlyContinue
try {
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class GGMLCudaRuntimeLoader
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr module);
}
'@
} finally {
    if ($null -eq $savedLib) {
        Remove-Item Env:LIB -ErrorAction SilentlyContinue
    } else {
        $env:LIB = $savedLib
    }
}

# LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR resolves dependencies beside the provider;
# LOAD_LIBRARY_SEARCH_DEFAULT_DIRS retains standard locations such as System32
# for the installed ARM64 MSVC runtime.
$module = [GGMLCudaRuntimeLoader]::LoadLibraryEx(
    $DllPath,
    [IntPtr]::Zero,
    [uint32]0x00001100)
if ($module -eq [IntPtr]::Zero) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "LoadLibraryEx failed for $DllPath (Win32 error $errorCode)."
}

if (-not [GGMLCudaRuntimeLoader]::FreeLibrary($module)) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "FreeLibrary failed for $DllPath (Win32 error $errorCode)."
}
