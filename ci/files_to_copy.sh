#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

# Move outside the github workspace to avoid conflicts
cd ..

# copy the build artifacts to a temporary directory
cp -R build/usr/* /tmp/rootfs/usr/
