#!/bin/bash
set -e

# Ensure we are in the script directory
cd "$(dirname "$0")"

echo "Building native libraries for all supported platforms using Docker..."

# Initialize submodules if not already done
pushd ..
if [ ! -f "piper-jni-native/src/main/native/piper/VERSION" ]; then
    echo "Initializing submodules..."
    git submodule update --init --recursive
fi
popd

# Build for Linux amd64, arm64, and armv7l
# The --output flag extracts the built libraries from the 'export' stage
# and places them into src/main/resources/
docker buildx build \
    --platform linux/amd64,linux/arm64,linux/arm/v7 \
    --target export \
    --output "type=local,dest=src/main/resources/temp_build" \
    .

# Move artifacts
if [ -d "src/main/resources/temp_build/linux_amd64" ]; then
    mv src/main/resources/temp_build/linux_amd64/*.zip src/main/resources/ 2>/dev/null || true
    mv src/main/resources/temp_build/linux_amd64/* src/main/resources/debian-amd64/
fi
if [ -d "src/main/resources/temp_build/linux_arm64" ]; then
    mv src/main/resources/temp_build/linux_arm64/* src/main/resources/debian-arm64/
fi
if [ -d "src/main/resources/temp_build/linux_arm_v7" ]; then
    mv src/main/resources/temp_build/linux_arm_v7/* src/main/resources/debian-armv7l/
fi

rm -rf src/main/resources/temp_build

echo "Build complete. Binaries are located in src/main/resources/"
ls src/main/resources
ls -R src/main/resources/debian-*
