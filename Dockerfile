# PTPN — Linux build image with vcpkg (Boost via manifest).
#
# Build:
#   docker build -t ptpn .
#
# Run (TDG example):
#   docker run --rm -v "$PWD/example:/data" ptpn tdg -f /data/l-bench/input.json
#
# ARM64 Linux host / Apple Silicon:
#   docker build --build-arg VCPKG_TARGET_TRIPLET=arm64-linux -t ptpn .

# syntax=docker/dockerfile:1

ARG UBUNTU_VERSION=24.04
ARG VCPKG_TARGET_TRIPLET=x64-linux

FROM ubuntu:${UBUNTU_VERSION} AS base

ARG VCPKG_TARGET_TRIPLET
ARG DEBIAN_FRONTEND=noninteractive

ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DEFAULT_TRIPLET=${VCPKG_TARGET_TRIPLET} \
    VCPKG_FEATURE_FLAGS=manifests \
    PATH="/opt/vcpkg:${PATH}"

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        g++ \
        ninja-build \
        pkg-config \
        cmake \
    && rm -rf /var/lib/apt/lists/*

# Pin vcpkg for reproducible dependency resolution (override with --build-arg).
ARG VCPKG_GIT_REF=2025.04.09
RUN git clone --depth 1 --branch "${VCPKG_GIT_REF}" \
        https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    || git clone --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics


FROM base AS builder

ARG VCPKG_TARGET_TRIPLET
ARG CMAKE_BUILD_TYPE=Release

WORKDIR /src

# Cache vcpkg manifest dependencies separately from source changes.
COPY vcpkg.json ./
RUN "${VCPKG_ROOT}/vcpkg" install \
        --triplet "${VCPKG_TARGET_TRIPLET}" \
        --x-manifest-root=/src \
        --x-install-root=/src/vcpkg_installed

COPY . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET}" \
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure


FROM ubuntu:${UBUNTU_VERSION} AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/ptpn /usr/local/bin/ptpn
COPY --from=builder /src/build/test/ptpn_test /usr/local/bin/ptpn_test

WORKDIR /data
ENTRYPOINT ["ptpn"]
CMD ["--help"]
