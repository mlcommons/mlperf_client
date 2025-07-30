<# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================#>


Param (
	[string]$qairt,
	[string]$model
)

$qairt = "qairt"
$python_version = python --version

Write-Host "CPU model of $model is getting generated"

if ($python_version -ge 3) {
	#Install dependencies for conversion
	python install_dependencies.py

	#Activate virtual environment
	.\bin_generation\Scripts\activate 

	#Run Conversion package
	python run_package.py --qairt $qairt --model $model

	#Deactivate virtual environment
	deactivate
} else {
	Write-Host "Not correct python is installed or python environment variable is not set, please install latest python 3 and ensure python environment variable is set properly"
}
