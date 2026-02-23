#Requires -Version 5.1
$ErrorActionPreference = "Stop"

# ==============================
# URL
# ==============================
$NLOHMANN_JSON_URL = "https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip"
$ONNXRUNTIME_URL = "https://github.com/microsoft/onnxruntime/releases/download/v1.24.2/onnxruntime-win-x64-gpu-1.24.2.zip"
$OPENCV_URL = "https://github.com/opencv/opencv/archive/refs/tags/4.13.0.tar.gz"
$EIGEN_URL = "https://codeload.github.com/eigen-mirror/eigen/tar.gz/refs/tags/3.4.0"

# ==============================
# 路径
# ==============================
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path

# 先构造路径，再创建目录，最后解析绝对路径
$THIRD_PARTY_RAW = "$SCRIPT_DIR\..\third_party"
if (-not (Test-Path $THIRD_PARTY_RAW)) {
    New-Item -ItemType Directory -Path $THIRD_PARTY_RAW -Force | Out-Null
}
$THIRD_PARTY_DIR = Resolve-Path $THIRD_PARTY_RAW

$TEMP_DIR = Join-Path $env:TEMP "third_party_build_$PID"

New-Item -ItemType Directory -Force -Path $THIRD_PARTY_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $TEMP_DIR | Out-Null

# ==============================
# 下载函数
# ==============================
function Download-File {
    param(
        [string]$Url,
        [string]$Out
    )

    Write-Host "Downloading $Url ..."

    if (Test-Path $Out) {
        Write-Host "File already exists: $Out" -ForegroundColor Yellow
        return
    }

    curl.exe -L -k --retry 3 --retry-delay 2 -o $Out $Url
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -Path $Out -ErrorAction SilentlyContinue  # 清理不完整的文件
        Write-Host "Download failed: $Url" -ForegroundColor Red
        throw "curl exited with code $LASTEXITCODE"
    }
}

# ==============================
# nlohmann/json
# ==============================
Write-Host "Setting up nlohmann/json ..."
$JSON_ZIP = Join-Path $TEMP_DIR "nlohmann.zip"
$JSON_EXTRACT = Join-Path $TEMP_DIR "nlohmann_extract"

Download-File -Url $NLOHMANN_JSON_URL -Out $JSON_ZIP
New-Item -ItemType Directory -Force -Path $JSON_EXTRACT | Out-Null

Expand-Archive -Path $JSON_ZIP -DestinationPath $JSON_EXTRACT -Force

# 找到 single_include/nlohmann 目录
$SINGLE_DIR = Get-ChildItem -Path $JSON_EXTRACT -Recurse -Directory -Filter "nlohmann" |
Where-Object { $_.Parent.Name -eq "single_include" } |
Select-Object -First 1 -ExpandProperty FullName

if (-not $SINGLE_DIR) {
    Write-Host "Error: single_include/nlohmann not found" -ForegroundColor Red
    exit 1
}

$DEST_JSON = Join-Path $THIRD_PARTY_DIR "nlohmann\include\nlohmann"
New-Item -ItemType Directory -Force -Path $DEST_JSON | Out-Null

Get-ChildItem -Path $SINGLE_DIR -Filter "*.hpp" | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $DEST_JSON -Force
}

# 清理
Remove-Item -Path $JSON_EXTRACT -Recurse -Force

# ==============================
# onnxruntime
# ==============================
Write-Host "Setting up onnxruntime ..."
$ORT_ZIP = Join-Path $TEMP_DIR "onnxruntime.zip"
$ORT_DEST = Join-Path $THIRD_PARTY_DIR "onnxruntime"
$ORT_EXTRACT = Join-Path $TEMP_DIR "onnxruntime_extract"

Download-File -Url $ONNXRUNTIME_URL -Out $ORT_ZIP
New-Item -ItemType Directory -Force -Path $ORT_EXTRACT | Out-Null

Expand-Archive -Path $ORT_ZIP -DestinationPath $ORT_EXTRACT -Force

# 通常解压后最外层就是一个版本目录
$TOP_DIR = Get-ChildItem -Path $ORT_EXTRACT -Directory | Select-Object -First 1

if (-not $TOP_DIR) {
    Write-Host "Error: cannot find onnxruntime top directory" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $ORT_DEST | Out-Null
Get-ChildItem -Path $TOP_DIR.FullName | ForEach-Object {
    Move-Item -Path $_.FullName -Destination $ORT_DEST -Force
}

Remove-Item -Path $ORT_EXTRACT -Recurse -Force

# ==============================
# Eigen
# ==============================
Write-Host "Setting up Eigen ..."
$EIGEN_TAR = Join-Path $TEMP_DIR "eigen.tar.gz"
$EIGEN_SOURCE = Join-Path $TEMP_DIR "eigen"
$EIGEN_DEST = Join-Path $THIRD_PARTY_DIR "eigen"

Download-File -Url $EIGEN_URL -Out $EIGEN_TAR
New-Item -ItemType Directory -Force -Path $EIGEN_SOURCE | Out-Null

# 使用 tar 解压 (PowerShell 5.1+ 和 Windows 10+ 支持)
tar -xzf $EIGEN_TAR -C $EIGEN_SOURCE --strip-components=1

Push-Location $EIGEN_SOURCE
New-Item -ItemType Directory -Force -Path "build" | Out-Null
Set-Location "build"

cmake -G "MinGW Makefiles" `
    -DCMAKE_INSTALL_PREFIX="$EIGEN_DEST" `
    -DBUILD_TESTING=OFF `
    -DEIGEN_BUILD_DOC=OFF `
    -DEIGEN_TEST_CXX11=OFF `
    -DEIGEN_TEST_CXX17=OFF `
    -DEIGEN_TEST_CXX20=OFF `
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON `
    ..

cmake --build . --config Release --parallel $env:NUMBER_OF_PROCESSORS
cmake --install .

Pop-Location

# ==============================
# OpenCV
# ==============================
Write-Host "Setting up OpenCV ..."
$OPENCV_TAR = Join-Path $TEMP_DIR "opencv.tar.gz"
$OPENCV_SOURCE = Join-Path $TEMP_DIR "opencv"
$OPENCV_DEST = Join-Path $THIRD_PARTY_DIR "opencv"

Download-File -Url $OPENCV_URL -Out $OPENCV_TAR
New-Item -ItemType Directory -Force -Path $OPENCV_SOURCE | Out-Null

tar -xzf $OPENCV_TAR -C $OPENCV_SOURCE --strip-components=1

Push-Location $OPENCV_SOURCE
New-Item -ItemType Directory -Force -Path "build" | Out-Null
Set-Location "build"

# 配置和编译 OpenCV
cmake -G "MinGW Makefiles" `
    -DCMAKE_INSTALL_PREFIX="$OPENCV_DEST" `
    -DCMAKE_BUILD_TYPE=Release `
    `
    -DWITH_EIGEN=ON `
    -DEigen3_DIR="$EIGEN_DEST\share\eigen3\cmake" `
    `
    -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_opencv_world=OFF `
    `
    -DBUILD_opencv_core=ON `
    -DBUILD_opencv_imgproc=ON `
    -DBUILD_opencv_highgui=ON `
    -DBUILD_opencv_imgcodecs=ON `
    `
    -DBUILD_opencv_dnn=OFF `
    -DBUILD_opencv_dnn_objdetect=OFF `
    -DBUILD_opencv_gapi=OFF `
    -DBUILD_opencv_features2d=OFF `
    -DBUILD_opencv_flann=OFF `
    -DBUILD_opencv_ml=OFF `
    -DBUILD_opencv_photo=OFF `
    -DBUILD_opencv_shape=OFF `
    -DBUILD_opencv_stitching=OFF `
    -DBUILD_opencv_video=OFF `
    -DBUILD_opencv_videoio=OFF `
    -DBUILD_opencv_calib3d=OFF `
    -DBUILD_opencv_objdetect=OFF `
    `
    -DBUILD_opencv_apps=OFF `
    -DBUILD_opencv_java=OFF `
    -DBUILD_opencv_python2=OFF `
    -DBUILD_opencv_python3=OFF `
    -DBUILD_TESTS=OFF `
    -DBUILD_PERF_TESTS=OFF `
    -DBUILD_EXAMPLES=OFF `
    -DBUILD_DOCS=OFF `
    `
    -DBUILD_JPEG=ON `
    -DBUILD_PNG=ON `
    -DBUILD_WEBP=ON `
    -DBUILD_TIFF=OFF `
    -DBUILD_OPENJPEG=OFF `
    -DBUILD_JASPER=OFF `
    -DBUILD_ZLIB=OFF `
    -DBUILD_PROTOBUF=OFF `
    `
    -DWITH_FFMPEG=OFF `
    -DWITH_GSTREAMER=OFF `
    -DWITH_OPENCL=OFF `
    -DWITH_OPENGL=OFF `
    -DWITH_QT=OFF `
    -DWITH_GTK=OFF `
    -DWITH_WIN32UI=ON `
    -DWITH_MSMF=OFF `
    -DWITH_DSHOW=OFF `
    -DWITH_DIRECTX=OFF `
    -DWITH_TBB=OFF `
    -DWITH_OPENMP=OFF `
    -DWITH_LAPACK=OFF `
    -DWITH_OPENEXR=OFF `
    `
    -DENABLE_PRECOMPILED_HEADERS=OFF `
    -DOPENCV_ENABLE_NONFREE=OFF `
    -DOPENCV_GENERATE_PKGCONFIG=OFF `
    ..

cmake --build . --config Release --parallel $env:NUMBER_OF_PROCESSORS
cmake --install .

Pop-Location

# ==============================
# 最终清理
# ==============================
Remove-Item -Path $TEMP_DIR -Recurse -Force

Write-Host "Third-party setup complete."
