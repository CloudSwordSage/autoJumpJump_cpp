#!/usr/bin/env bash
set -euo pipefail

# ==============================
# URL
# ==============================
NLOHMANN_JSON_URL="https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip"
ONNXRUNTIME_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.24.2/onnxruntime-win-x64-gpu-1.24.2.zip"
OPENCV_URL="https://github.com/opencv/opencv/archive/refs/tags/4.13.0.tar.gz"
EIGEN_URL="https://codeload.github.com/eigen-mirror/eigen/tar.gz/refs/tags/3.4.0"

# ==============================
# 路径
# ==============================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_RAW="$SCRIPT_DIR/../third_party"
if [[ ! -d "$THIRD_PARTY_RAW" ]]; then
    mkdir -p "$THIRD_PARTY_RAW"
fi
THIRD_PARTY_DIR="$SCRIPT_DIR/../third_party"
TEMP_DIR="/tmp/third_party_build_$$"

mkdir -p "$THIRD_PARTY_DIR" "$TEMP_DIR"

# ==============================
# 下载函数
# ==============================
download_file() {
    local url="$1"
    local out="$2"
    echo "Downloading $url ..."
    if [[ -f "$out" ]]; then
        echo "File already exists: $out"
        return 0
    fi
    curl -L --fail --progress-bar -o "$out" "$url" || {
        echo "Download failed: $url" >&2
        return 1
    }
}

# ==============================
# nlohmann/json
# ==============================
echo "Setting up nlohmann/json ..."
JSON_ZIP="$TEMP_DIR/nlohmann.zip"
JSON_EXTRACT="$TEMP_DIR/nlohmann_extract"

download_file "$NLOHMANN_JSON_URL" "$JSON_ZIP"
mkdir -p "$JSON_EXTRACT"
unzip -q -o "$JSON_ZIP" -d "$JSON_EXTRACT"

# 找到 single_include/nlohmann 目录
SINGLE_DIR=$(find "$JSON_EXTRACT" -type d -path "*/single_include/nlohmann" | head -n 1)

if [[ -z "$SINGLE_DIR" ]]; then
    echo "Error: single_include/nlohmann not found" >&2
    exit 1
fi

DEST_JSON="$THIRD_PARTY_DIR/nlohmann/include/nlohmann"
mkdir -p "$DEST_JSON"

cp -f "$SINGLE_DIR"/*.hpp "$DEST_JSON/"

# 清理
rm -rf "$JSON_EXTRACT"

# ==============================
# onnxruntime
# ==============================
echo "Setting up onnxruntime ..."
ORT_ZIP="$TEMP_DIR/onnxruntime.zip"
ORT_DEST="$THIRD_PARTY_DIR/onnxruntime"
ORT_EXTRACT="$TEMP_DIR/onnxruntime_extract"

download_file "$ONNXRUNTIME_URL" "$ORT_ZIP"
mkdir -p "$ORT_EXTRACT"
unzip -q -o "$ORT_ZIP" -d "$ORT_EXTRACT"

# 通常解压后最外层就是一个版本目录
TOP_DIR=$(find "$ORT_EXTRACT" -mindepth 1 -maxdepth 1 -type d | head -n 1)

if [[ -z "$TOP_DIR" || ! -d "$TOP_DIR" ]]; then
    echo "Error: cannot find onnxruntime top directory" >&2
    exit 1
fi

mkdir -p "$ORT_DEST"
mv "$TOP_DIR"/* "$ORT_DEST"/ 2>/dev/null || true

rm -rf "$ORT_EXTRACT"

# ==============================
# Eigen
# ==============================
echo "Setting up Eigen ..."
EIGEN_TAR="$TEMP_DIR/eigen.tar.gz"
EIGEN_SOURCE="$TEMP_DIR/eigen"
EIGEN_DEST="$THIRD_PARTY_DIR/eigen"

download_file "$EIGEN_URL" "$EIGEN_TAR"
mkdir -p "$EIGEN_SOURCE"
tar -xzf "$EIGEN_TAR" -C "$EIGEN_SOURCE" --strip-components=1

cd "$EIGEN_SOURCE"
mkdir -p build
cd build

cmake .. -G "MSYS Makefiles" \
    -DCMAKE_INSTALL_PREFIX="$EIGEN_DEST" \
    -DBUILD_TESTING=OFF \
    -DEIGEN_BUILD_DOC=OFF \
    -DEIGEN_TEST_CXX11=OFF \
    -DEIGEN_TEST_CXX17=OFF \
    -DEIGEN_TEST_CXX20=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build . --config Release --parallel $(nproc)
cmake --install .

# ==============================
# OpenCV
# ==============================
echo "Setting up OpenCV ..."
OPENCV_TAR="$TEMP_DIR/opencv.tar.gz"
OPENCV_SOURCE="$TEMP_DIR/opencv"
OPENCV_DEST="$THIRD_PARTY_DIR/opencv"

download_file "$OPENCV_URL" "$OPENCV_TAR"
mkdir -p "$OPENCV_SOURCE"
tar -xzf "$OPENCV_TAR" -C "$OPENCV_SOURCE" --strip-components=1

cd "$OPENCV_SOURCE"
mkdir -p build
cd build

# 配置和编译 OpenCV
cmake .. -G "MSYS Makefiles" \
    -DCMAKE_INSTALL_PREFIX="$OPENCV_DEST" \
    -DCMAKE_BUILD_TYPE=Release \
    \
    -DWITH_EIGEN=ON \
    -DEigen3_DIR="$EIGEN_DEST/share/eigen3/cmake" \
    \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_opencv_world=OFF \
    \
    -DBUILD_opencv_core=ON \
    -DBUILD_opencv_imgproc=ON \
    -DBUILD_opencv_highgui=ON \
    -DBUILD_opencv_imgcodecs=ON \
    \
    -DBUILD_opencv_dnn=OFF \
    -DBUILD_opencv_dnn_objdetect=OFF \
    -DBUILD_opencv_gapi=OFF \
    -DBUILD_opencv_features2d=OFF \
    -DBUILD_opencv_flann=OFF \
    -DBUILD_opencv_ml=OFF \
    -DBUILD_opencv_photo=OFF \
    -DBUILD_opencv_shape=OFF \
    -DBUILD_opencv_stitching=OFF \
    -DBUILD_opencv_video=OFF \
    -DBUILD_opencv_videoio=OFF \
    -DBUILD_opencv_calib3d=OFF \
    -DBUILD_opencv_objdetect=OFF \
    \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_python2=OFF \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DOCS=OFF \
    \
    -DBUILD_JPEG=ON \
    -DBUILD_PNG=ON \
    -DBUILD_WEBP=ON \
    -DBUILD_TIFF=OFF \
    -DBUILD_OPENJPEG=OFF \
    -DBUILD_JASPER=OFF \
    -DBUILD_ZLIB=OFF \
    -DBUILD_PROTOBUF=OFF \
    \
    -DWITH_FFMPEG=OFF \
    -DWITH_GSTREAMER=OFF \
    -DWITH_OPENCL=OFF \
    -DWITH_OPENGL=OFF \
    -DWITH_QT=OFF \
    -DWITH_GTK=OFF \
    -DWITH_WIN32UI=ON \
    -DWITH_MSMF=OFF \
    -DWITH_DSHOW=OFF \
    -DWITH_DIRECTX=OFF \
    -DWITH_TBB=OFF \
    -DWITH_OPENMP=OFF \
    -DWITH_LAPACK=OFF \
    -DWITH_OPENEXR=OFF \
    \
    -DENABLE_PRECOMPILED_HEADERS=OFF \
    -DOPENCV_ENABLE_NONFREE=OFF \
    -DOPENCV_GENERATE_PKGCONFIG=OFF

cmake --build . --config Release --parallel $(nproc)
cmake --install .

# ==============================
# 最终清理
# ==============================
rm -rf "$TEMP_DIR"

echo "Third-party setup complete."
