SHELL := /bin/sh
GO_CACHE_DIR := $(CURDIR)/.cache/go-build

.PHONY: test test-kernel test-cli test-integration build check-cmake check-make check-go

test: test-kernel test-cli test-integration

test-kernel: check-cmake check-make
	@command -v timeout >/dev/null 2>&1 || { echo "Error: timeout is required for kernel tests."; exit 1; }
	@sh tests/kernel/run.sh

test-cli: check-go
	@mkdir -p "$(GO_CACHE_DIR)"
	@cd tools/cli && GOCACHE="$(GO_CACHE_DIR)" go test ./...

test-integration: check-cmake check-make check-go
	@mkdir -p "$(GO_CACHE_DIR)"
	@cd tools/cli && GOCACHE="$(GO_CACHE_DIR)" go test -tags integration ./cmd/

build: check-cmake check-make
	@cmake -S . -B build64 -DFROTH_CELL_SIZE_BITS=32
	@cmake --build build64

check-cmake:
	@command -v cmake >/dev/null 2>&1 || { echo "Error: cmake is required but was not found on PATH."; exit 1; }

check-make:
	@command -v make >/dev/null 2>&1 || { echo "Error: make is required but was not found on PATH."; exit 1; }

check-go:
	@command -v go >/dev/null 2>&1 || { echo "Error: go is required but was not found on PATH."; exit 1; }
