#!/usr/bin/env bash
# Fetches the pinned JUCE version into external/JUCE (not committed to git).
set -euo pipefail
cd "$(dirname "$0")/.."
JUCE_TAG=8.0.15
if [ ! -d external/JUCE ]; then
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git external/JUCE
fi
