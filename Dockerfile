# PTPN — Linux build image with vcpkg (Boost via manifest).
#
# Build (native arch — arm64 on Apple Silicon, amd64 on Intel):
#   docker build -t ptpn .
#
# Force x86_64 Linux:
#   docker build --platform linux/amd64 --build-arg VCPKG_TARGET_TRIPLET=x64-linux -t ptpn .
#
# Run (TDG example):
#   docker run --rm -v "$PWD/example:/data" ptpn tdg -f /data/l-bench/input.json

# syntax=docker/dockerfile:1

ARG UBUNTU_VERSION=24.04
# Default triplet follows the build platform (arm64-linux on Apple Silicon Docker).
ARG VCPKG_TARGET_TRIPLET

FROM ubuntu:${UBUNTU_VERSION} AS base

ARG TARGETARCH
ARG VCPKG_TARGET_TRIPLET
ARG DEBIAN_FRONTEND=noninteractive

# Resolve triplet when the caller does not pass VCPKG_TARGET_TRIPLET.
RUN case "${TARGETARCH:-amd64}" in \
        arm64|arm) echo "arm64-linux" > /opt/vcpkg-default-triplet ;; \
        *) echo "x64-linux" > /opt/vcpkg-default-triplet ;; \
    esac

ENV VCPKG_ROOT=/opt/vcpkg \
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
        zip \
        unzip \
        tar \
    && rm -rf /var/lib/apt/lists/*

# Pin vcpkg for reproducible dependency resolution (override with --build-arg).
ARG VCPKG_GIT_REF=2025.04.09
RUN git clone --depth 1 --branch "${VCPKG_GIT_REF}" \
        https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    || git clone --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

RUN if [ -n "${VCPKG_TARGET_TRIPLET}" ]; then \
        echo "${VCPKG_TARGET_TRIPLET}" > /etc/vcpkg-triplet; \
    else \
        cp /opt/vcpkg-default-triplet /etc/vcpkg-triplet; \
    fi


FROM base AS builder

ARG CMAKE_BUILD_TYPE=Release

WORKDIR /src

# Cache vcpkg manifest dependencies separately from source changes.
COPY vcpkg.json ./
RUN TRIPLET="$(cat /etc/vcpkg-triplet)" \
    && "${VCPKG_ROOT}/vcpkg" install \
        --triplet "${TRIPLET}" \
        --x-manifest-root=/src \
        --x-install-root=/src/vcpkg_installed

COPY . .

RUN TRIPLET="$(cat /etc/vcpkg-triplet)" \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="${TRIPLET}" \
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
