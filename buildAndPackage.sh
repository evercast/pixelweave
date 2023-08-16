#!/usr/bin/env bash

set -e -u -o pipefail

rm -rf build
mkdir -p build

cmake -G Xcode -B build -S . -DCMAKE_CONFIGURATION_TYPES=Release
xcodebuild -project build/PixelWeave.xcodeproj -scheme PixelWeave build
open build/Release
