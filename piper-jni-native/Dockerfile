# syntax=docker/dockerfile:1

# Stage 1: Native library compilation
# This stage runs on the target platform (using QEMU if needed)
FROM maven:3.9.9-eclipse-temurin-17-focal AS native-builder
# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends build-essential curl
# Add kitware repo for newer cmake (improves armv7l support)
RUN curl -s https://apt.kitware.com/kitware-archive.sh | bash -s
# Install cmake
RUN apt-get update && apt-get install -y --no-install-recommends cmake

WORKDIR /app

COPY CMakeLists.txt .
COPY build_linux.sh .
COPY src src

ARG TARGETARCH

# Submodules should be populated by the host before build
RUN TARGETARCH=${TARGETARCH} ./build_linux.sh
RUN mkdir -p /app/install
RUN if [ ${TARGETARCH} = "arm" ]; then cp -r src/main/resources/debian-armv7l/* /app/install/; else cp -r src/main/resources/debian-${TARGETARCH}/* /app/install/; fi
RUN if [ ${TARGETARCH} = "amd64" ]; then cp src/main/resources/*.zip /app/install/; fi

# Stage 2: Export binaries
# This stage is used to extract the built libraries from the image
FROM scratch AS export
COPY --from=native-builder /app/install/* /
