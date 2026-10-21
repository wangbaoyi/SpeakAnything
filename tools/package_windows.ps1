param(
    [string]$Configuration = 'Release',
    [string]$Version = '0.2.0',
    [switch]$SkipBuild
)

# Builds the portable ZIP: executables, Qt runtime, dictionaries and every
# model under modelspt/ (see README "Models"). Models are not in git.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $root 'build'
$binaryDirectory = Join-Path $buildDirectory $Configuration
$stagingRoot = Join-Path $root 'out\package'
$packageDirectory = Join-Path $stagingRoot "SpeakAnything-$Version-windows-x64"
$archivePath = Join-Path $root "out\SpeakAnything-$Version-windows-x64.zip"

if (-not $SkipBuild) {
    & cmake --build $buildDirectory --config $Configuration --target sensevoice-ui sensevoice-stream sensevoice-tts --parallel 8
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
}

$modelsSource = Join-Path $root 'modelspt'
if (-not (Test-Path -LiteralPath $modelsSource)) { $modelsSource = Join-Path $root 'models' }
$requiredModels = @(
    @{ Path = 'sensevoice-small-q8.gguf'; Kind = 'recognizer' },
    @{ Path = 'fsmn-vad.gguf'; Kind = 'recognizer' },
    @{ Path = 'opus-mt-zh-en-ct2\model.bin'; Kind = 'translator' },
    @{ Path = 'opus-mt-en-zh-ct2\model.bin'; Kind = 'translator' },
    @{ Path = 'kokoro-multi-lang-v1_1\model.onnx'; Kind = 'voice' }
)
foreach ($model in $requiredModels) {
    $candidates = @(
        (Join-Path $modelsSource (Join-Path $model.Kind $model.Path)),
        (Join-Path $modelsSource $model.Path)
    )
    if (-not ($candidates | Where-Object { Test-Path -LiteralPath $_ })) {
        throw "Missing model: $($model.Kind)\$($model.Path) or $($model.Path) under $modelsSource (see README)"
    }
}

Remove-Item -LiteralPath $packageDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null

foreach ($file in @('sensevoice-ui.exe', 'sensevoice-stream.exe', 'sensevoice-tts.exe',
                    'sherpa-onnx-c-api.dll', 'onnxruntime.dll', 'onnxruntime_providers_shared.dll')) {
    Copy-Item -LiteralPath (Join-Path $binaryDirectory $file) -Destination $packageDirectory -Force
}

# The CUDA translation build links CTranslate2 as a DLL.
$ct2Dll = Join-Path $binaryDirectory 'ctranslate2.dll'
if (Test-Path -LiteralPath $ct2Dll) {
    Copy-Item -LiteralPath $ct2Dll -Destination $packageDirectory -Force
}

# The ggml CUDA backend is linked statically, but the CUDA runtime and cuBLAS
# stay dynamic. CTranslate2 CUDA also loads them at runtime
# (CUDA_DYNAMIC_LOADING=ON). Copy them when either backend was compiled in.
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
$ggmlCuda = Select-String -Path $cachePath -Pattern '^GGML_CUDA:BOOL=ON$' -Quiet
$ct2Cuda = (Select-String -Path $cachePath -Pattern '^SENSEVOICE_CT2_CUDA:BOOL=ON$' -Quiet) -and
    (Select-String -Path $cachePath -Pattern '^SENSEVOICE_CT2_CUDA_AVAILABLE:INTERNAL=ON$' -Quiet)
$cudaBuild = $ggmlCuda -or $ct2Cuda
if ($cudaBuild) {
    $cudaBin = $null
    $cudaCache = Select-String -Path $cachePath -Pattern '^CUDAToolkit_BIN_DIR:PATH=(.*)$' |
        Select-Object -First 1
    if ($cudaCache) { $cudaBin = $cudaCache.Matches[0].Groups[1].Value }
    if (-not $cudaBin -and $env:CUDA_PATH) { $cudaBin = Join-Path $env:CUDA_PATH 'bin' }
    if (-not $cudaBin -or -not (Test-Path -LiteralPath $cudaBin)) {
        throw 'The build uses CUDA but the CUDA bin directory was not found.'
    }
    $cudaDlls = Get-ChildItem -LiteralPath $cudaBin -Filter '*.dll' |
        Where-Object { $_.Name -match '^(cudart64_|cublas64_|cublasLt64_)' }
    if (-not $cudaDlls) { throw "Missing CUDA runtime DLLs in $cudaBin." }
    foreach ($dll in $cudaDlls) {
        Copy-Item -LiteralPath $dll.FullName -Destination $packageDirectory -Force
    }
    Write-Output "Packaged CUDA runtime: $($cudaDlls.Name -join ', ')"
}

$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    $qtDir = (Select-String -Path (Join-Path $buildDirectory 'CMakeCache.txt') -Pattern '^Qt6_DIR:PATH=(.*)$').Matches[0].Groups[1].Value
    $windeployqt = Join-Path $qtDir '..\..\..\bin\windeployqt.exe'
} else {
    $windeployqt = $windeployqt.Source
}
# windeployqt prints warnings on stderr; judge it by its exit code only.
$ErrorActionPreference = 'Continue'
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $packageDirectory 'sensevoice-ui.exe') 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

# CTranslate2 is built with MSVC OpenMP; ship its runtime next to the executables.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$openMpRuntime = & $vswhere -latest -products * -find 'VC\Redist\MSVC\**\x64\Microsoft.VC*.OpenMP\vcomp140.dll' |
    Where-Object { $_ -notmatch '\\onecore\\' } | Select-Object -First 1
if (-not $openMpRuntime) { throw 'Missing vcomp140.dll in the Visual Studio redistributable directory.' }
Copy-Item -LiteralPath $openMpRuntime -Destination $packageDirectory -Force

$dictionary = Join-Path $packageDirectory 'dict'
New-Item -ItemType Directory -Force -Path $dictionary | Out-Null
foreach ($file in @('jieba.dict.utf8', 'user.dict.utf8')) {
    Copy-Item -LiteralPath (Join-Path $root "third_party\cppjieba\dict\$file") -Destination $dictionary -Force
}

$packageModels = Join-Path $packageDirectory 'modelspt'
New-Item -ItemType Directory -Force -Path $packageModels | Out-Null
foreach ($category in @('recognizer', 'translator', 'voice')) {
    $source = Join-Path $modelsSource $category
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $packageModels -Recurse -Force
    }
}
# Flat layouts from older installs.
foreach ($entry in @('sensevoice-small-q8.gguf', 'fsmn-vad.gguf', 'opus-mt-zh-en-ct2', 'opus-mt-en-zh-ct2', 'kokoro-multi-lang-v1_1')) {
    $source = Join-Path $modelsSource $entry
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $packageModels -Recurse -Force
    }
}

foreach ($file in @('install.ps1', 'install.cmd', 'uninstall.ps1', 'README.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "packaging\windows\$file") -Destination $packageDirectory -Force
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $packageDirectory -Force
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $packageDirectory -Force

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archivePath) | Out-Null
Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archivePath -CompressionLevel Optimal
Write-Output "Package: $archivePath"
