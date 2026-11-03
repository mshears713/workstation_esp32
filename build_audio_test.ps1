$ErrorActionPreference='Stop'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\python_env\idf5.5_py3.11_env'
$env:PATH="$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
. C:\Espressif\frameworks\esp-idf-v5.5.3\export.ps1
python C:\Espressif\frameworks\esp-idf-v5.5.3\tools\idf.py build *> audio-test-integrated-build.log
$buildExit=$LASTEXITCODE
Get-Content audio-test-integrated-build.log -Tail 55
exit $buildExit
