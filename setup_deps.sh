#!/bin/bash
set -e

echo "==> Initializing submodule..."
git submodule update --init --recursive

echo "==> Building raylib..."
mkdir -p external/raylib/build
pushd external/raylib/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
make -j
popd

echo "==> Copying headers and library..."
mkdir -p lib include
cp external/raylib/build/raylib/libraylib.a lib/
cp -r external/raylib/src/*.h include/

echo "==> Done."

