#!/bin/bash

# Script to validate RPM build environment and diagnose network issues

set -e

echo "=== DocumentDB RPM Build Environment Validation ==="
echo

# Check Docker availability
echo "1. Checking Docker installation..."
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed or not in PATH"
    echo "   Please install Docker to build RPM packages"
    exit 1
fi
echo "✅ Docker is available"

# Check Docker daemon
echo "2. Checking Docker daemon..."
if ! docker info &> /dev/null; then
    echo "❌ Docker daemon is not running or not accessible"
    echo "   Please start Docker daemon and ensure you have proper permissions"
    exit 1
fi
echo "✅ Docker daemon is running"

# Test network connectivity for common package repositories
echo "3. Testing network connectivity..."

repositories=(
    "mirrors.rockylinux.org"
    "download.postgresql.org"
    "mirrorlist.centos.org"
    "registry.access.redhat.com"
)

failed_repos=()

for repo in "${repositories[@]}"; do
    echo "   Testing access to $repo..."
    if curl -s --connect-timeout 10 --max-time 15 "https://$repo" > /dev/null 2>&1 || \
       curl -s --connect-timeout 10 --max-time 15 "http://$repo" > /dev/null 2>&1; then
        echo "   ✅ $repo is accessible"
    else
        echo "   ❌ $repo is not accessible"
        failed_repos+=("$repo")
    fi
done

# Test base image pull
echo "4. Testing base image access..."
if docker pull rockylinux:8 &> /dev/null; then
    echo "✅ Rocky Linux 8 base image is accessible"
    docker rmi rockylinux:8 &> /dev/null || true
else
    echo "❌ Cannot pull Rocky Linux 8 base image"
    failed_repos+=("docker.io/rockylinux")
fi

echo

if [ ${#failed_repos[@]} -eq 0 ]; then
    echo "🎉 Environment validation passed! You should be able to build RPM packages."
    echo
    echo "To build RPM packages, run:"
    echo "  ./packaging/build_packages.sh --os rhel8 --pg 16"
    echo "  ./packaging/build_packages.sh --os rhel9 --pg 17"
else
    echo "⚠️  Environment validation found issues:"
    echo
    echo "The following repositories are not accessible:"
    for repo in "${failed_repos[@]}"; do
        echo "  - $repo"
    done
    echo
    echo "This indicates network restrictions (firewall/proxy) that will prevent RPM builds."
    echo
    echo "SOLUTIONS:"
    echo
    echo "1. Contact your system/network administrator to:"
    echo "   - Allow access to the blocked repositories above"
    echo "   - Configure proxy settings if needed"
    echo "   - Add domains to firewall allowlist"
    echo
    echo "2. Try building from a different network environment with fewer restrictions"
    echo
    echo "3. Use pre-built packages if available, or build in an unrestricted environment"
    echo
    echo "For more information, see the Network Requirements section in packaging/README.md"
fi

echo
echo "=== Validation Complete ==="