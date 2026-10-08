#!/usr/bin/env bash
# Sets up what the CI jobs (and anyone reproducing them) need, from scratch and
# repeatably. Pinned: Zephyr v4.4.2, Zephyr SDK 1.0.1 (the versions this project
# was developed with). Safe to run again: it skips what is already in place.
#
#   tools/ci_setup.sh workspace   Zephyr source + only the modules this project needs
#   tools/ci_setup.sh sdk         Zephyr SDK with the ARM toolchain (firmware build only)
#
# Environment: CI_ROOT (default: current directory) is where zephyrproject/ and
# zephyr-sdk-<version>/ are created.
set -euo pipefail

ZEPHYR_VERSION="${ZEPHYR_VERSION:-v4.4.2}"
SDK_VERSION="${SDK_VERSION:-1.0.1}"
ROOT="$(cd "${CI_ROOT:-.}" && pwd)"

# Modules found by building this project: CMSIS (FFT), Nordic HAL (nRF52840),
# ST HAL (LSM9DS1 driver) and mbedTLS + TF-PSA-Crypto (Bluetooth security).
MODULES="cmsis cmsis_6 cmsis-dsp hal_nordic hal_st mbedtls tf-psa-crypto"

# SHA-256 of zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz (from the release's sha256.sum)
SDK_MINIMAL_SHA256="ca9bc0ff66fafca1dac9d592a36d953cf16d096a9d09b1c0357f021cf9f6a7eb"
SDK_URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VERSION}/zephyr-sdk-${SDK_VERSION}_linux-x86_64_minimal.tar.xz"

setup_workspace() {
	python3 -m pip install --quiet west
	mkdir -p "$ROOT/zephyrproject"
	cd "$ROOT/zephyrproject"
	if [ ! -d zephyr/.git ]; then
		git clone --quiet --depth 1 --branch "$ZEPHYR_VERSION" https://github.com/zephyrproject-rtos/zephyr zephyr
	fi
	if [ ! -d .west ]; then
		west init -l zephyr
	fi
	# shellcheck disable=SC2086   # the module list is meant to word-split
	west update $MODULES --narrow -o=--depth=1
	python3 -m pip install --quiet -r zephyr/scripts/requirements-base.txt
	echo "Zephyr workspace ready in $ROOT/zephyrproject (Zephyr $ZEPHYR_VERSION)"
}

setup_sdk() {
	local dir="$ROOT/zephyr-sdk-${SDK_VERSION}"
	if [ ! -x "$dir/setup.sh" ]; then
		if [ "$SDK_VERSION" != "1.0.1" ]; then
			echo "SDK_MINIMAL_SHA256 above is only valid for SDK 1.0.1" >&2
			exit 1
		fi
		curl -fsSL -o "$ROOT/sdk-minimal.tar.xz" "$SDK_URL"
		echo "${SDK_MINIMAL_SHA256}  $ROOT/sdk-minimal.tar.xz" | sha256sum -c -
		tar -xf "$ROOT/sdk-minimal.tar.xz" -C "$ROOT"
		rm -f "$ROOT/sdk-minimal.tar.xz"
	fi
	# ARM toolchain and host tools (dtc etc.); the CMake package is not registered:
	# builds point at the SDK with ZEPHYR_SDK_INSTALL_DIR instead.
	"$dir/setup.sh" -t arm-zephyr-eabi -h
	echo "Zephyr SDK ready in $dir (use ZEPHYR_SDK_INSTALL_DIR=$dir)"
}

case "${1:-}" in
	workspace) setup_workspace ;;
	sdk)       setup_sdk ;;
	*)         echo "usage: $0 workspace|sdk" >&2; exit 2 ;;
esac
