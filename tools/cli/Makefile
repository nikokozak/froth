REPO_ROOT := ../..
SDK_PAYLOAD_DIR := internal/sdk/generated
# Repo-local checkout builds keep a checkout-specific suffix.
OUTPUT ?= froth-cli
export FROTH_LINK_MAX_PAYLOAD ?= 1024
empty :=
space := $(empty) $(empty)
LOCAL_RUNTIME_TEST_NAMES := TestIntegrationConnectLocalBuildsAndLaunchesBinary TestIntegrationToolingControlSessionLocalRuntime
BUILD_PROJECT_INTEGRATION_TEST_NAMES := TestIntegrationNewBuildAndRunBoot TestIntegrationMultiFileProjectBuildsAndResolvesDependencies TestIntegrationBuildCleanRemovesArtifacts TestIntegrationDoctorShowsProjectInfo
KNOWN_INTEGRATION_TEST_NAMES := $(sort $(LOCAL_RUNTIME_TEST_NAMES) $(BUILD_PROJECT_INTEGRATION_TEST_NAMES))
LOCAL_RUNTIME_TESTS := ^($(subst $(space),|,$(LOCAL_RUNTIME_TEST_NAMES)))$$
BUILD_PROJECT_INTEGRATION_TESTS := ^($(subst $(space),|,$(BUILD_PROJECT_INTEGRATION_TEST_NAMES)))$$

.PHONY: sdk-payload build build-test-runner test test-unit test-localruntime test-integration check-integration-tests

# The repo root is the source of truth.
sdk-payload:
	@echo "  Generating embedded CLI SDK payload..."
	@go run ./internal/sdk/cmd/generate -repo $(REPO_ROOT) -out $(SDK_PAYLOAD_DIR)

build: sdk-payload
	@rm -f frothy-cli
	@echo "  Compiling repo-local CLI binary..."
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go build -o $(OUTPUT) .; \
	else \
		go build -o $(OUTPUT) .; \
	fi

build-test-runner:
	@echo "  Compiling $(OUTPUT)..."
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go build -o $(OUTPUT) ./cmd/test-runner; \
	else \
		go build -o $(OUTPUT) ./cmd/test-runner; \
	fi

test: test-unit test-localruntime test-integration

test-unit: sdk-payload
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go test ./...; \
	else \
		go test ./...; \
	fi

check-integration-tests:
	@integration_files=$$(find cmd -name '*_test.go' -type f -print | xargs grep -l '^//go:build integration$$' | LC_ALL=C sort); \
	bad_files=$$(printf '%s\n' "$$integration_files" | grep -vE '^cmd/[^/]*integration_test[.]go$$' || true); \
	if [ -n "$$bad_files" ]; then \
		printf 'integration tests must live in cmd/*integration_test.go so test-all lane ownership is explicit:\n%s\n' "$$bad_files" >&2; \
		exit 1; \
	fi; \
	all_tests=$$(grep -hE '^func Test[A-Za-z0-9_]*' $$integration_files | sed 's/^func //; s/(.*//' | grep -v '^TestMain$$' | LC_ALL=C sort | tr '\n' ' ' | sed 's/[[:space:]]*$$//'); \
	bad_names=$$(printf '%s\n' "$$all_tests" | tr ' ' '\n' | grep -v '^TestIntegration' || true); \
	if [ -n "$$bad_names" ]; then \
		printf 'integration tests must use TestIntegration* names so the focused lanes fail closed:\n%s\n' "$$bad_names" >&2; \
		exit 1; \
	fi; \
	actual=$$(printf '%s\n' "$$all_tests"); \
	expected="$(KNOWN_INTEGRATION_TEST_NAMES)"; \
	if [ "$$actual" != "$$expected" ]; then \
		printf 'integration test list drifted; assign each TestIntegration* case to LOCAL_RUNTIME_TEST_NAMES or BUILD_PROJECT_INTEGRATION_TEST_NAMES\nexpected: %s\nactual:   %s\n' "$$expected" "$$actual" >&2; \
		exit 1; \
	fi

test-localruntime: sdk-payload check-integration-tests
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go test -tags integration -run '$(LOCAL_RUNTIME_TESTS)' ./cmd; \
	else \
		go test -tags integration -run '$(LOCAL_RUNTIME_TESTS)' ./cmd; \
	fi

test-integration: sdk-payload check-integration-tests
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go test -tags integration -run '$(BUILD_PROJECT_INTEGRATION_TESTS)' ./cmd; \
	else \
		go test -tags integration -run '$(BUILD_PROJECT_INTEGRATION_TESTS)' ./cmd; \
	fi
