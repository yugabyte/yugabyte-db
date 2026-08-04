#!/bin/bash

# Test script to validate error handling for invalid initialization data
# This script tests that DocumentDB properly handles invalid JS files and exits gracefully

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Test configuration
CONTAINER_NAME="documentdb-invalid-init-test"
IMAGE_NAME="documentdb-gateway-test"
INVALID_DATA_DIR="$SCRIPT_DIR/sample-invalid-data"
DOCKERFILE_PATH="$PROJECT_ROOT/.github/containers/Build-Ubuntu/Dockerfile_gateway"
DOCUMENTDB_PORT="10260"
PASSWORD="TestPassword123"
TEST_TIMEOUT=300  # 5 minutes timeout for container to stop

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== DocumentDB Invalid Init-Data Test ===${NC}"
echo "Project Root: $PROJECT_ROOT"
echo "Script Directory: $SCRIPT_DIR"
echo "Container: $CONTAINER_NAME"
echo "Image: $IMAGE_NAME"
echo "Invalid Data Directory: $INVALID_DATA_DIR"
echo "Dockerfile: $DOCKERFILE_PATH"
echo "DocumentDB Port: $DOCUMENTDB_PORT"
echo "Test Timeout: ${TEST_TIMEOUT}s"
echo

# Function to print colored output
print_status() {
    local status=$1
    local message=$2
    case $status in
        "SUCCESS")
            echo -e "${GREEN}✅ $message${NC}"
            ;;
        "FAILURE")
            echo -e "${RED}❌ $message${NC}"
            ;;
        "INFO")
            echo -e "${YELLOW}ℹ️  $message${NC}"
            ;;
        *)
            echo "$message"
            ;;
    esac
}

# Function to check if mongosh is available
check_mongosh() {
    print_status "INFO" "Checking Prerequisites"
    if ! command -v mongosh >/dev/null 2>&1; then
        print_status "FAILURE" "mongosh is not installed or not in PATH"
        echo "Please install MongoDB Shell (mongosh) to run this test."
        echo "Visit: https://docs.mongodb.com/mongodb-shell/install/"
        exit 1
    fi
    print_status "SUCCESS" "mongosh is available: $(mongosh --version)"
    echo
}

# Function to build the Docker image
build_image() {
    print_status "INFO" "Building Docker Image"
    if [ ! -f "$DOCKERFILE_PATH" ]; then
        print_status "FAILURE" "Dockerfile not found at $DOCKERFILE_PATH"
        exit 1
    fi
    
    echo "Building image $IMAGE_NAME from $DOCKERFILE_PATH..."
    echo "=== Docker Build Output ==="
    
    # Build with verbose output
    if docker build -f "$DOCKERFILE_PATH" -t "$IMAGE_NAME" "$PROJECT_ROOT" --no-cache 2>&1 | tee /tmp/docker_build.log; then
        print_status "SUCCESS" "Docker image built successfully"
        echo "=== End of Docker Build Output ==="
    else
        print_status "FAILURE" "Failed to build Docker image"
        echo "=== Docker Build Error Output ==="
        cat /tmp/docker_build.log
        echo "=== End of Docker Build Error Output ==="
        exit 1
    fi
    echo
}

# Function to cleanup previous runs
cleanup() {
    print_status "INFO" "Cleaning up previous containers..."
    docker stop $CONTAINER_NAME 2>/dev/null || true
    docker rm $CONTAINER_NAME 2>/dev/null || true
    
    # Clean up temporary log files
    rm -f /tmp/docker_build.log /tmp/container_final.log /tmp/container_analysis.log
    
    print_status "SUCCESS" "Cleanup completed"
}

# Function to wait for container to stop and verify exit code
wait_for_container_exit() {
    print_status "INFO" "Waiting for container to stop (timeout: ${TEST_TIMEOUT}s)..."
    local start_time=$(date +%s)
    local timeout=$TEST_TIMEOUT
    
    while [ $(($(date +%s) - start_time)) -lt $timeout ]; do
        # Check if container is still running
        if ! docker ps -q -f name=$CONTAINER_NAME | grep -q .; then
            print_status "SUCCESS" "Container stopped automatically"
            
            # Get container exit code
            local exit_code=$(docker inspect $CONTAINER_NAME --format='{{.State.ExitCode}}')
            echo "Container exit code: $exit_code"
            
            # Show final container logs immediately when it stops
            echo
            echo "=== Final Container Logs (when container stopped) ==="
            docker logs $CONTAINER_NAME 2>&1 | tee /tmp/container_final.log
            echo "=== End of Final Container Logs ==="
            echo
            
            # Exit code should be non-zero for failure
            if [ "$exit_code" -ne 0 ]; then
                print_status "SUCCESS" "Container exited with error code $exit_code (expected for invalid data)"
                return 0
            else
                print_status "FAILURE" "Container exited with success code $exit_code (unexpected for invalid data)"
                return 1
            fi
        fi
        
        echo "Container still running... ($(($(($(date +%s) - start_time))))s elapsed)"
        
        # Show live container logs every 10 seconds
        if [ $(($(($(date +%s) - start_time)) % 10)) -eq 0 ]; then
            echo "=== Live Container Logs ($(($(($(date +%s) - start_time))))s elapsed) ==="
            docker logs --tail 20 $CONTAINER_NAME 2>&1
            echo "=== End of Live Container Logs ==="
        fi
        
        sleep 5
    done
    
    print_status "FAILURE" "Container did not stop within timeout"
    echo "=== Container Logs on Timeout ==="
    docker logs $CONTAINER_NAME 2>&1
    echo "=== End of Container Logs on Timeout ==="
    return 1
}

# Function to analyze container logs for proper error messages
analyze_logs() {
    print_status "INFO" "Analyzing container logs for error handling..."
    
    # Get all container logs
    local logs=$(docker logs $CONTAINER_NAME 2>&1)
    
    echo
    echo "=== Complete Container Logs Analysis ==="
    echo "$logs"
    echo "=== End of Complete Container Logs Analysis ==="
    echo
    
    # Save logs to file for debugging
    echo "$logs" > /tmp/container_analysis.log
    print_status "INFO" "Container logs saved to /tmp/container_analysis.log"
    
    # Check for various error indicators
    local error_found=false
    
    # Check for initialization script execution
    if echo "$logs" | grep -q "init_documentdb_data.sh\|Starting DocumentDB data initialization\|Processing initialization scripts"; then
        print_status "SUCCESS" "Init data script was executed"
    else
        print_status "FAILURE" "Init data script was not executed"
        error_found=true
    fi
    
    # Check for mongosh execution
    if echo "$logs" | grep -q "mongosh\|Executing initialization script"; then
        print_status "SUCCESS" "mongosh was attempted to run"
    else
        print_status "FAILURE" "mongosh was not attempted"
        error_found=true
    fi
    
    # Check for JavaScript syntax errors
    if echo "$logs" | grep -qi "syntax.*error\|unexpected.*token\|parse.*error"; then
        print_status "SUCCESS" "JavaScript syntax error detected in logs"
    else
        print_status "INFO" "No JavaScript syntax errors found in logs"
    fi
    
    # Check for MongoDB errors
    if echo "$logs" | grep -qi "mongodb.*error\|duplicate.*key\|operation.*failed"; then
        print_status "SUCCESS" "MongoDB operation error detected in logs"
    else
        print_status "INFO" "No MongoDB operation errors found in logs"
    fi
    
    # Check for script failure messages
    if echo "$logs" | grep -qi "failed.*to.*execute\|error.*executing\|initialization.*failed"; then
        print_status "SUCCESS" "Script failure messages detected"
    else
        print_status "FAILURE" "No script failure messages found"
        error_found=true
    fi
    
    # Check that it didn't complete successfully
    if echo "$logs" | grep -q "Sample data initialization completed!"; then
        print_status "FAILURE" "Container reported successful initialization (unexpected for invalid data)"
        error_found=true
    else
        print_status "SUCCESS" "Container did not report successful initialization (expected for invalid data)"
    fi
    
    if [ "$error_found" = true ]; then
        return 1
    else
        return 0
    fi
}

# Function to run a test with specific invalid data
run_invalid_data_test() {
    local test_name=$1
    local invalid_file=$2
    
    print_status "INFO" "Running test: $test_name"
    echo "Invalid file: $invalid_file"
    
    # Create a temporary directory with only the invalid file
    local temp_dir=$(mktemp -d)
    cp "$invalid_file" "$temp_dir/"
    
    echo "Test data files:"
    ls -la "$temp_dir"/*.js
    echo
    
    # Run the container with invalid data
    print_status "INFO" "Starting DocumentDB container with invalid data..."
    
    echo "=== Docker Run Command ==="
    echo "docker run -d \\"
    echo "    --name $CONTAINER_NAME \\"
    echo "    -p $DOCUMENTDB_PORT:$DOCUMENTDB_PORT \\"
    echo "    -e PASSWORD=$PASSWORD \\"
    echo "    -v \"$temp_dir:/init_doc_db.d\" \\"
    echo "    $IMAGE_NAME \\"
    echo "    --password $PASSWORD \\"
    echo "    --init-data-path /init_doc_db.d"
    echo "=== End of Docker Run Command ==="
    echo
    
    if docker run -d \
        --name $CONTAINER_NAME \
        -p $DOCUMENTDB_PORT:$DOCUMENTDB_PORT \
        -e PASSWORD=$PASSWORD \
        -v "$temp_dir:/init_doc_db.d" \
        $IMAGE_NAME \
        --password $PASSWORD \
        --init-data-path /init_doc_db.d; then
        
        print_status "SUCCESS" "Container started successfully"
        local container_id=$(docker ps -q -f name=$CONTAINER_NAME)
        echo "Container ID: $container_id"
        
        # Show immediate logs after container start
        echo
        echo "=== Initial Container Logs (first 5 seconds) ==="
        sleep 5
        docker logs $CONTAINER_NAME 2>&1
        echo "=== End of Initial Container Logs ==="
        echo
        
        # Wait for container to stop due to error
        if wait_for_container_exit; then
            # Analyze logs for proper error handling
            if analyze_logs; then
                print_status "SUCCESS" "Test '$test_name' passed - container handled invalid data correctly"
                local test_result=0
            else
                print_status "FAILURE" "Test '$test_name' failed - error handling was not proper"
                local test_result=1
            fi
        else
            print_status "FAILURE" "Test '$test_name' failed - container did not stop properly"
            echo "=== Debug: Container Status ==="
            docker ps -a -f name=$CONTAINER_NAME
            echo "=== Debug: Container Logs ==="
            docker logs $CONTAINER_NAME 2>&1
            echo "=== End of Debug Information ==="
            local test_result=1
        fi
    else
        print_status "FAILURE" "Failed to start container"
        echo "=== Debug: Docker Run Error ==="
        docker logs $CONTAINER_NAME 2>&1 || echo "No container logs available"
        echo "=== End of Docker Run Error ==="
        local test_result=1
    fi
    
    # Cleanup
    cleanup
    rm -rf "$temp_dir"
    
    return $test_result
}

# Main test execution
main() {
    print_status "INFO" "Starting DocumentDB Invalid Init-Data Test"
    
    # Check prerequisites
    check_mongosh
    
    # Cleanup any previous test runs
    cleanup
    
    # Build the Docker image
    build_image
    
    # Verify invalid data files exist
    if [ ! -d "$INVALID_DATA_DIR" ]; then
        print_status "FAILURE" "Invalid data directory not found at $INVALID_DATA_DIR"
        exit 1
    fi
    
    print_status "SUCCESS" "Invalid data directory found"
    echo "Available invalid data files:"
    ls -la "$INVALID_DATA_DIR"/*.js
    echo
    
    print_status "INFO" "Docker system information:"
    echo "Docker version: $(docker --version)"
    echo "Docker info:"
    docker info | head -20
    echo
    
    print_status "INFO" "System information:"
    echo "OS: $(uname -s)"
    echo "Architecture: $(uname -m)"
    echo "Available memory: $(free -h | grep '^Mem:' | awk '{print $2}')"
    echo "Available disk: $(df -h . | tail -1 | awk '{print $4}')"
    echo
    
    # Test results tracking
    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    
    # Run tests with different types of invalid data
    for invalid_file in "$INVALID_DATA_DIR"/*.js; do
        if [ -f "$invalid_file" ]; then
            total_tests=$((total_tests + 1))
            local test_name=$(basename "$invalid_file" .js)
            
            echo
            echo "=========================================="
            print_status "INFO" "TEST $total_tests: $test_name"
            echo "=========================================="
            
            if run_invalid_data_test "$test_name" "$invalid_file"; then
                passed_tests=$((passed_tests + 1))
                print_status "SUCCESS" "Test $total_tests passed"
            else
                failed_tests=$((failed_tests + 1))
                print_status "FAILURE" "Test $total_tests failed"
            fi
        fi
    done
    
    # Final test summary
    echo
    echo "=========================================="
    print_status "INFO" "TEST SUMMARY"
    echo "=========================================="
    echo "Total tests run: $total_tests"
    echo "Tests passed: $passed_tests"
    echo "Tests failed: $failed_tests"
    echo
    
    if [ $failed_tests -eq 0 ]; then
        print_status "SUCCESS" "ALL TESTS PASSED!"
        echo "✅ Docker build completed successfully"
        echo "✅ All invalid data was handled correctly"
        echo "✅ Containers stopped automatically with proper error codes"
        echo "✅ Error messages were properly logged"
        echo
        print_status "INFO" "Log files created during testing:"
        echo "- Docker build log: /tmp/docker_build.log"
        echo "- Container final log: /tmp/container_final.log"
        echo "- Container analysis log: /tmp/container_analysis.log"
        exit 0
    else
        print_status "FAILURE" "SOME TESTS FAILED!"
        echo "❌ $failed_tests out of $total_tests tests failed"
        echo "Please check the detailed results above."
        echo
        print_status "INFO" "Debug log files available:"
        echo "- Docker build log: /tmp/docker_build.log"
        echo "- Container final log: /tmp/container_final.log"
        echo "- Container analysis log: /tmp/container_analysis.log"
        echo
        echo "Use 'cat /tmp/docker_build.log' to view Docker build details"
        echo "Use 'cat /tmp/container_final.log' to view final container output"
        echo "Use 'cat /tmp/container_analysis.log' to view container analysis"
        exit 1
    fi
}

# Run the test
main "$@"
