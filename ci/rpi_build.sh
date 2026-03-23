#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
set -ex

# Source SDK environment 
## <strong>ToDo</strong>: Need to update for SDK path and GITHUB_WORKSPACE handling.
source ${GITHUB_WORKSPACE}/install/environment-setup-cortexa7t2hf-neon-vfpv4-poky-linux-gnueabi

# Make sure we are in the right directory
cd ${GITHUB_WORKSPACE}

# Set LDFLAGS
export TARGET_CC_ARCH="${LDFLAGS}"
export LDFLAGS="${LDFLAGS} -pthread -lar-gpr -ltinyalsa"

# Create and enter build directory
mkdir -p build && cd build

# Configure with CMake
cmake ${BUILD_ARGS} ..

# Build and install
make -j$(nproc)
make DESTDIR=${GITHUB_WORKSPACE}/build install
