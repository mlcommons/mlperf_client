import papermill as pm
import os, sys
import nbformat
import re

model_name = sys.argv[1]

print("#=====================================Setting up qairt sdk path============================================#")
qairt_path = '../qairt'
if not os.path.exists(qairt_path):
    print(f"Qualcomm's QAIRT SDK is not present at {qairt_path}, please ensure that it is present.")
    sys.exit(1)
qairt_path = os.path.abspath(qairt_path)
qairt_version_path = os.listdir(qairt_path)[0]
print(f"Qairt Version Used: {qairt_version_path}")
QNN_SDK_ROOT = os.path.join(qairt_path, qairt_version_path) + "/"
if model_name == "phi3.5":
    model_id = os.path.abspath(os.path.join(qairt_path, "..", "adascale_dir_temp")) + "/"
elif model_name == "llama3.1":
    model_id = os.path.abspath(os.path.join(qairt_path, "..", "adascale_dir_temp")) + "/"
elif model_name == "phi4":
    model_id = os.path.abspath(os.path.join(qairt_path, "..", "Phi-4-reasoning")) + "/"
else:
    print(f"Model not supported")
    sys.exit(1)
    
print(f"QNN SDK ROOT: {QNN_SDK_ROOT} model_id: {model_id}")

parameters={"QNN_SDK_ROOT": QNN_SDK_ROOT, "model_id": model_id}



def modify_notebook(notebook_path, output_path, variable_changes):

    with open(notebook_path, "r", encoding="utf-8") as f:
        nb = nbformat.read(f, as_version=4)

    for cell in nb.cells:
        if cell.cell_type == "code" and isinstance(cell.source, str):
            for var_name, new_value in variable_changes.items():
                if ((model_name == "llama3.1" or model_name == "phi3.5") and var_name == "model_id"):
                    pattern = r'model_id\s*=\s*os\.getenv\(["\']MODEL_ID["\'](?:\s*,\s*[^)]*)?\)'
                elif ((model_name == "llama3.1" or model_name == "phi3.5") and var_name == "QNN_SDK_ROOT"):
                    pattern = r'QNN_SDK_ROOT\s*=\s*os\.getenv\(["\']QNN_SDK_ROOT["\'](?:\s*,\s*[^)]*)?\)'
                elif model_name == "phi4":
                    pattern = rf"^\s*{re.escape(var_name)}\s*=\s*os\.getenv\(\s*['\"][^'\"]+['\"]\s*(?:,\s*[^)]*)?\)"
                replacement = f"{var_name} = {repr(new_value)}"
                cell.source = re.sub(pattern, replacement, cell.source, flags=re.MULTILINE)

    with open(output_path, "w", encoding="utf-8") as f:
        nbformat.write(nb, f)

if model_name == "phi3.5":
    modify_notebook("Phi3p5.ipynb", "updated-Phi3p5.ipynb", parameters)
    pm.execute_notebook(
        "updated-Phi3p5.ipynb",
        "output-Phi3p5.ipynb",
        kernel_name="python"
    )
elif model_name == "llama3.1":
    modify_notebook("llama3_1.ipynb", "updated-llama3.1.ipynb", parameters)
    pm.execute_notebook(
        "updated-llama3.1.ipynb",
        "output-llama3.1.ipynb",
        kernel_name="python"
    )
elif model_name == "phi4":
    modify_notebook("Phi4_Reasoning.ipynb", "updated-phi4.ipynb", parameters)
    pm.execute_notebook(
        "updated-phi4.ipynb",
        "output-phi4.ipynb",
        kernel_name="python"
    )