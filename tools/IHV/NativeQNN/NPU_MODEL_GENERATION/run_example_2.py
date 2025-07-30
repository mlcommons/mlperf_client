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
if model_name == "llama2":
    model_dir = os.path.abspath(os.path.join(qairt_path, "..", "example-1/output_dir")) + "/"
elif model_name == "phi3.5":
    model_dir = os.path.abspath(os.path.join(qairt_path, "..", "example1/output_dir")) + "/"
elif model_name == "llama3.1":
    model_dir = os.path.abspath(os.path.join(qairt_path, "..", "Step-1/output_dir")) + "/"
else:
    print(f"Model not supported")
    sys.exit(1)

print(f"QNN SDK ROOT: {QNN_SDK_ROOT} MODELS_DIR: {model_dir}")

parameters={"QNN_SDK_ROOT": QNN_SDK_ROOT, "MODELS_DIR": model_dir}

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
                if var_name == "MODELS_DIR" and (model_name == "llama3.1" or model_name == "phi3.5"):
                    pattern = re.compile(r'MODELS_DIR\s*=\s*Path\(\s*os\.getenv\(\s*["\']INPUT_MODEL_DIR["\']\s*, \s*[^)]+\)\s*\)')
                    replacement = f'MODELS_DIR = Path("{to_raw_string(new_value)}")'
                    print("Replacement: " + replacement)
                elif var_name == "QNN_SDK_ROOT" and (model_name == "llama3.1" or model_name == "phi3.5"):
                    pattern = re.compile(r'QNN_SDK_ROOT\s*=\s*Path\(\s*os\.getenv\(\s*["\']QNN_SDK_ROOT["\']\s*, \s*[^)]+\)\s*\)')
                    replacement = f'QNN_SDK_ROOT = Path("{to_raw_string(new_value)}")'
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

def convert_line_endings(file_path):
    with open(file_path, 'r') as file:
        content = file.read()
 
    # Replace Windows line endings with Unix line endings
    content = content.replace('\r\n', '\n')
 
    with open(file_path, 'w') as file:
        file.write(content)
 
# Define the mha2sha root path
mha2sha_root_path = "./../G2G/MHA2SHA"
if not os.path.exists(mha2sha_root_path):
    print(f"MHA2SHA path is not present at {mha2sha_root_path}, please ensure that it is present.")
    sys.exit(1)
script_path = os.path.join(os.path.abspath(mha2sha_root_path), "bin", "mha2sha-onnx-converter")
 
# Convert line endings
convert_line_endings(script_path)

if (model_name == "llama2"):
    try:
        pm.execute_notebook(
            "qnn_model_prepare.ipynb",
            "output_qnn_model_prepare.ipynb",
            kernel_name="python",
            execution_timeout=-1
        )
    except Exception as e:
        print(f"Bins are generated but notebook is not able to run fully due to IO Pub Output timeout constraint of device")
elif model_name == "phi3.5":
    modify_notebook("qnn_model_prepare.ipynb", "updated_qnn_model_prepare.ipynb", parameters)
    pm.execute_notebook(
        "updated_qnn_model_prepare.ipynb",
        "output_qnn_model_prepare.ipynb",
        kernel_name="python",
        execution_timeout=-1
    )
elif model_name == "llama3.1":
    modify_notebook("qnn_model_prepare.ipynb", "updated_qnn_model_prepare.ipynb", parameters)
    pm.execute_notebook(
        "updated_qnn_model_prepare.ipynb",
        "output_qnn_model_prepare.ipynb",
        kernel_name="python",
        execution_timeout=-1
    )