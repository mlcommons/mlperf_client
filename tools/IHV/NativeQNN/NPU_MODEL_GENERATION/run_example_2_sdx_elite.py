import papermill as pm
import os, sys
import nbformat
import re

model_name = sys.argv[1]

print("#=====================================Setting up qairt sdk path============================================#")
qairt_path = '../../qairt'
if not os.path.exists(qairt_path):
    print(f"Qualcomm's QAIRT SDK is not present at {qairt_path}, please ensure that it is present.")
    sys.exit(1)
qairt_path = os.path.abspath(qairt_path)
qairt_version_path = os.listdir(qairt_path)[0]
print(f"Qairt Version Used: {qairt_version_path}")
QNN_SDK_ROOT = os.path.join(qairt_path, qairt_version_path) + "/"
if model_name == "phi3.5":
    model_dir = os.path.abspath(os.path.join(qairt_path, "..", "Step-1/output_dir_phi35")) + "/"
elif model_name == "llama3.1":
    model_dir = os.path.abspath(os.path.join(qairt_path, "..", "Step-1/output_dir")) + "/"
else:
    print(f"Model not supported")
    sys.exit(1)

print(f"QNN SDK ROOT: {QNN_SDK_ROOT} MODELS_DIR: {model_dir}")

platform_gen = 2
parameters={"QNN_SDK_ROOT": QNN_SDK_ROOT, "MODELS_DIR": model_dir, "PLATFORM_GEN": platform_gen, "EXTENDED_UDMA": False}

def to_raw_string(s):
    return s.replace('\\', '\\\\')

def modify_notebook(notebook_path, output_path, variable_changes):

    with open(notebook_path, "r", encoding="utf-8") as f:
        nb = nbformat.read(f, as_version=4)
    pattern = r''
    replacement = r''
    for cell in nb.cells:
        if cell.cell_type == "code":
            for var_name, new_value in variable_changes.items():
                if var_name == "MODELS_DIR" and (model_name == "llama3.1"):
                    pattern = re.compile(r'MODELS_DIR\s*=\s*Path\(\s*os\.getenv\(\s*["\']INPUT_MODEL_DIR["\']\s*, \s*[^)]+\)\s*\)')
                    replacement = f'MODELS_DIR = Path("{to_raw_string(new_value)}")'
                    print("Replacement: " + replacement)
                elif var_name == "QNN_SDK_ROOT" and (model_name == "llama3.1"):
                    pattern = re.compile(r'QNN_SDK_ROOT\s*=\s*Path\(\s*os\.getenv\(\s*["\']QNN_SDK_ROOT["\']\s*, \s*[^)]+\)\s*\)')
                    replacement = f'QNN_SDK_ROOT = Path("{to_raw_string(new_value)}")'
                    print("Replacement: " + replacement)
                elif var_name == "MODELS_DIR" and (model_name == "phi3.5"):
                    pattern = re.compile(r'^\s*MODELS_DIR\s*=\s*os\.getenv\(\s*["\']INPUT_MODEL_DIR["\']\s*,\s*[^)]*\)\s*$',re.MULTILINE)
                    replacement = f'MODELS_DIR = Path("{to_raw_string(new_value)}")'
                    print("Replacement: " + replacement)
                elif var_name == "QNN_SDK_ROOT" and (model_name == "phi3.5"):
                    pattern = re.compile(r'^\s*QNN_SDK_ROOT\s*=\s*os\.getenv\(\s*["\']QNN_SDK_ROOT["\']\s*,\s*[^)]*\)\s*$',re.MULTILINE)
                    replacement = f'QNN_SDK_ROOT = Path("{to_raw_string(new_value)}")'
                    print("Replacement: " + replacement)
                elif var_name == "PLATFORM_GEN":
                    pattern = re.compile(r'^\s*PLATFORM_GEN\s*=\s*int\(\s*os\.getenv\(\s*["\']PLATFORM_GEN["\']\s*,\s*3\s*\)\s*\)\s*$',re.MULTILINE)
                    replacement = f'PLATFORM_GEN = int(os.getenv("PLATFORM_GEN", 2))'
                    print("Replacement: " + replacement)
                elif var_name == "EXTENDED_UDMA":
                    pattern = re.compile(r'"extended_udma"\s*:\s*(True|False)\s*,?\s*')
                    replacement = ''
                    print("Replacement: " + replacement)
                else:
                    pattern = re.compile(r'QNN_SDK_ROOT\s*=\s*Path\((["\']).*?\1\)')
                    replacement = f'QNN_SDK_ROOT = Path("{to_raw_string(new_value)}")'
                    print("Replacement: " + replacement)

                lines = cell.source.splitlines()
                new_lines = [pattern.sub(replacement, line) for line in lines]
                cell.source = "\n".join(new_lines)

    with open(output_path, "w", encoding="utf-8") as f:
        nbformat.write(nb, f)

if model_name == "phi3.5":
    modify_notebook("qnn_model_prepare.ipynb", "updated_qnn_model_prepare_sdx_elite.ipynb", parameters)
    pm.execute_notebook(
        "updated_qnn_model_prepare_sdx_elite.ipynb",
        "output_qnn_model_prepare_sdx_elite.ipynb",
        kernel_name="python",
        execution_timeout=-1
    )
elif model_name == "llama3.1":
    modify_notebook("qnn_model_prepare.ipynb", "updated_qnn_model_prepare_sdx_elite.ipynb", parameters)
    pm.execute_notebook(
        "updated_qnn_model_prepare_sdx_elite.ipynb",
        "output_qnn_model_prepare_sdx_elite.ipynb",
        kernel_name="python",
        execution_timeout=-1
    )