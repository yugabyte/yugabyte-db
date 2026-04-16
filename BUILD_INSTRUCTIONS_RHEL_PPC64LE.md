# YugabyteDB Build Instructions for RHEL ppc64le

This document provides complete instructions for building YugabyteDB on Red Hat Enterprise Linux (RHEL) for the ppc64le (IBM POWER) architecture.

## System Requirements

- **OS**: RHEL 8 or RHEL 9 (ppc64le)
- **Architecture**: ppc64le (POWER8 or later recommended)
- **RAM**: Minimum 8GB, recommended 16GB+
- **Disk**: Minimum 50GB free space
- **CPU**: POWER8, POWER9, or POWER10 processor

## Prerequisites Installation

### 1. Enable Required Repositories

```bash
# For RHEL 8:
sudo subscription-manager repos --enable codeready-builder-for-rhel-8-ppc64le-rpms
sudo dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm

# For RHEL 9:
sudo subscription-manager repos --enable codeready-builder-for-rhel-9-ppc64le-rpms
sudo dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
```

### 2. Install Development Tools

```bash
# Install base development tools
sudo dnf groupinstall -y "Development Tools"

# Install additional build dependencies
sudo dnf install -y \
  gcc \
  gcc-c++ \
  make \
  cmake \
  autoconf \
  automake \
  libtool \
  pkg-config \
  git \
  wget \
  curl \
  rsync \
  gettext \
  bison \
  flex \
  patch \
  diffutils \
  which \
  file
```

### 3. Install Required Libraries

```bash
# Install system libraries
sudo dnf install -y \
  libatomic \
  libstdc++-devel \
  libstdc++-static \
  glibc-devel \
  glibc-static \
  zlib-devel \
  openssl-devel \
  readline-devel \
  ncurses-devel \
  libuuid-devel \
  libedit-devel \
  libicu-devel \
  libxml2-devel \
  libxslt-devel \
  krb5-devel \
  pam-devel \
  systemd-devel \
  libaio-devel \
  numactl-devel \
  perl \
  perl-core \
  perl-IPC-Run \
  perl-Test-Simple \
  perl-FindBin \
  perl-File-Compare \
  perl-File-Copy \
  perl-lib \
  perl-Digest-SHA \
  perl-ExtUtils-MakeMaker \
  unzip \
  java-11-openjdk-devel \
  maven

# Note: libunwind-devel is not available for ppc64le on RHEL
# The build system will use alternative stack unwinding mechanisms
```

### 4. Install Python 3.11

```bash
# Install Python 3.11 (required by third-party build system)
# For RHEL 8, you may need to enable additional repositories
sudo dnf install -y python3.11 python3.11-devel python3.11-pip

# Create python3.11 symlink if needed
sudo alternatives --install /usr/bin/python3.11 python3.11 /usr/bin/python3.11 1

# Verify Python 3.11 installation
python3.11 --version

# Upgrade pip for Python 3.11
python3.11 -m pip install --upgrade pip

# Install required Python packages
python3.11 -m pip install --user \
  'cmake>=3.31' \
  pyyaml \
  requests \
  distro

# Also install for default python3 (for compatibility)
sudo dnf install -y python3 python3-devel python3-pip
python3 -m pip install --user \
  'cmake>=3.31' \
  pyyaml \
  requests \
  distro
```

### 5. Install Java Development Kit

```bash
# Java and Maven are already installed in step 3
# Set JAVA_HOME environment variable
export JAVA_HOME=/usr/lib/jvm/java-11-openjdk
echo 'export JAVA_HOME=/usr/lib/jvm/java-11-openjdk' >> ~/.bashrc

# Verify Java installation
java -version
javac -version
mvn --version
```

### 6. Install Node.js (for UI components)

```bash
# NodeSource doesn't support ppc64le, so we'll build from source or use EPEL
# Option 1: Install from EPEL (recommended - easier but may be older version)
sudo dnf install -y nodejs npm

# Verify Node.js installation
node --version
npm --version

# If Node.js version is too old (< 16.x), build from source:
# NODE_VERSION=18.19.0
# wget https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}.tar.gz
# tar -xzf node-v${NODE_VERSION}.tar.gz
# cd node-v${NODE_VERSION}
# ./configure --prefix=/usr/local
# make -j$(nproc)
# sudo make install
# cd ..
# rm -rf node-v${NODE_VERSION} node-v${NODE_VERSION}.tar.gz

# Install global npm packages that may be needed
npm install -g node-gyp
```

### 7. Install Rust (required for Python cryptography package)

```bash
# Install Rust using rustup (required for building Python cryptography package)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

# Source Rust environment
source "$HOME/.cargo/env"
echo 'source "$HOME/.cargo/env"' >> ~/.bashrc

# Verify Rust installation
rustc --version
cargo --version
```

### 8. Install Go (for node-agent and yugabyted-ui)

```bash
# Download and install Go 1.21 or later for ppc64le
GO_VERSION=1.21.5
wget https://go.dev/dl/go${GO_VERSION}.linux-ppc64le.tar.gz
sudo rm -rf /usr/local/go
sudo tar -C /usr/local -xzf go${GO_VERSION}.linux-ppc64le.tar.gz
rm go${GO_VERSION}.linux-ppc64le.tar.gz

# Add Go to PATH
export PATH=$PATH:/usr/local/go/bin
echo 'export PATH=$PATH:/usr/local/go/bin' >> ~/.bashrc

# Verify Go installation
go version
```

### 9. Install Bazel (required for building Abseil and other dependencies)

```bash
# Install Bazelisk (Bazel version manager) which will download the correct Bazel version
# Bazelisk is a wrapper that automatically downloads and runs the correct Bazel version
GO_VERSION_CHECK=$(go version | grep -oP 'go\K[0-9.]+' | head -1)
if [ -z "$GO_VERSION_CHECK" ]; then
    echo "Error: Go is not installed. Please install Go first."
    exit 1
fi

# Install Bazelisk using Go
go install github.com/bazelbuild/bazelisk@latest

# Create bazel symlink
sudo ln -sf ~/go/bin/bazelisk /usr/local/bin/bazel

# Add Go bin to PATH if not already there
export PATH=$PATH:$HOME/go/bin
echo 'export PATH=$PATH:$HOME/go/bin' >> ~/.bashrc

# Verify Bazel installation
bazel --version

# Note: Bazelisk will automatically download Bazel 5.3.1 (as specified in .bazelversion)
# when it's first used during the build process
```

### 10. Configure Locale

```bash
# Ensure en_US.UTF-8 locale is available (required by initdb)
sudo dnf install -y glibc-langpack-en
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8
```

## Pre-Build Verification

Before starting the build, verify all prerequisites are correctly installed:

```bash
# Verify all required tools and versions
echo "=== Checking Prerequisites ==="

# Check GCC version (should be 11.x or later)
gcc --version | head -1

# Check Python 3.11
python3.11 --version

# Check Java
java -version

# Check Maven
mvn --version | head -1

# Check Node.js
node --version

# Check Go
go version

# Check Rust
rustc --version

# Check Perl modules
perl -MFindBin -e 'print "FindBin: OK\n"'
perl -MFile::Compare -e 'print "File::Compare: OK\n"'
perl -MFile::Copy -e 'print "File::Copy: OK\n"'

# Check locale
locale | grep -E "LANG|LC_ALL"

echo "=== Prerequisites Check Complete ==="
```

If any check fails, revisit the corresponding installation step.

## Building YugabyteDB

### 1. Clone the Repository

```bash
# Clone YugabyteDB repository
git clone https://github.com/yugabyte/yugabyte-db.git
cd yugabyte-db
```

### 2. Set Environment Variables

```bash
# Set target architecture
export YB_TARGET_ARCH=ppc64le

# Set compiler (use system GCC)
export YB_COMPILER_TYPE=gcc

# Optional: Set number of parallel jobs (adjust based on available CPU cores)
export YB_MAKE_PARALLELISM=$(nproc)
```

### 3. Build Third-Party Dependencies

```bash
# IMPORTANT: There are no prebuilt third-party archives for ppc64le
# All third-party dependencies must be built from source
# This step takes significant time (2-4 hours depending on hardware)

# Set environment variable to force building from source
export YB_DOWNLOAD_THIRDPARTY=0

# Build third-party dependencies from source with GCC compiler
./build-support/invoke_thirdparty_build.sh --build-type uninstrumented --compiler-family gcc
```

### 4. Build YugabyteDB

```bash
# Build YugabyteDB in release mode
# This includes the database core, tools, and tests
./yb_build.sh release

# For a faster build without building tests (but tests can still be run):
./yb_build.sh release --skip-build-tests

# For a minimal build (database only, no Java/UI):
./yb_build.sh release daemons initdb --sj --skip-pg-parquet --no-odyssey --no-ybc
```

### 5. Build Output

After successful build, binaries will be located in:
```
build/release-gcc-dynamic-ppc64le-ninja/
├── bin/              # Database binaries (yb-master, yb-tserver, etc.)
├── lib/              # Shared libraries
├── postgres/         # PostgreSQL binaries and libraries
└── tests-*/          # Test binaries (if built)
```

## Running YugabyteDB

### 1. Initialize and Start a Local Cluster

```bash
# Navigate to build directory
cd build/release-gcc-dynamic-ppc64le-ninja

# Start a single-node cluster using yugabyted
./bin/yugabyted start

# Check cluster status
./bin/yugabyted status

# Connect to YSQL (PostgreSQL-compatible API)
./postgres/bin/ysqlsh

# Connect to YCQL (Cassandra-compatible API)
./bin/ycqlsh
```

### 2. Stop the Cluster

```bash
./bin/yugabyted stop
```

## Known Limitations and Workarounds

### 1. Prebuilt Binaries Not Available

Most third-party prebuilt binaries are not available for ppc64le. All dependencies must be built from source:

- **Node.js modules**: Native modules will be compiled during npm install
- **Third-party tools**: azcopy, some monitoring tools may not be available
- **YBC (YugabyteDB Controller)**: May need to be built from source

### 2. Architecture-Specific Optimizations

The build system has been configured with ppc64le-specific optimizations:

- **CPU Target**: `-mcpu=power8 -mtune=power9` for broad compatibility
- **Atomics**: Native 16-byte atomic support via quadword instructions
- **CRC32C**: Software implementation (no SSE4.2 equivalent)

### 3. Performance Considerations

- POWER8 is the minimum supported processor
- POWER9 or POWER10 recommended for optimal performance
- Ensure adequate cooling for sustained high-performance workloads

## Troubleshooting

### Build Failures

1. **Perl Module Missing (FindBin, File::Compare, etc.)**:
   ```bash
   # Install missing Perl modules
   sudo dnf install -y perl-core perl-FindBin perl-File-Compare perl-File-Copy perl-lib perl-Digest-SHA perl-ExtUtils-MakeMaker
   ```

2. **Compiler Family Error**:
   ```bash
   # Always specify --compiler-family gcc
   ./build-support/invoke_thirdparty_build.sh --build-type uninstrumented --compiler-family gcc
   ```

3. **No Third-Party Archives Found**:
   ```bash
   # This is expected for ppc64le - ensure YB_DOWNLOAD_THIRDPARTY=0 is set
   export YB_DOWNLOAD_THIRDPARTY=0
   ./build-support/invoke_thirdparty_build.sh --build-type uninstrumented --compiler-family gcc
   ```

4. **Out of Memory**:
   ```bash
   # Reduce parallelism
   export YB_MAKE_PARALLELISM=4
   ./yb_build.sh release
   ```

5. **Missing Dependencies**:
   ```bash
   # Install missing system packages
   sudo dnf install -y <package-name>
   ```

6. **Third-Party Build Issues**:
   ```bash
   # Clean and rebuild third-party
   rm -rf thirdparty/build thirdparty/installed
   export YB_DOWNLOAD_THIRDPARTY=0
   ./build-support/invoke_thirdparty_build.sh --build-type uninstrumented --compiler-family gcc
   ```

7. **Python Cryptography Build Failure**:
   ```bash
   # Ensure Rust is installed and in PATH
   source "$HOME/.cargo/env"
   rustc --version
   # Reinstall cryptography
   python3.11 -m pip install --user --force-reinstall cryptography
   ```

8. **Node.js Native Module Build Failure**:
   ```bash
   # Ensure node-gyp is installed
   npm install -g node-gyp
   # Clear npm cache
   npm cache clean --force
   # Rebuild
   npm rebuild
   ```

### Runtime Issues

1. **Locale Errors**:
   ```bash
   sudo dnf install -y glibc-langpack-en
   export LANG=en_US.UTF-8
   export LC_ALL=en_US.UTF-8
   ```

2. **Library Not Found**:
   ```bash
   # Add library paths
   export LD_LIBRARY_PATH=$PWD/lib:$PWD/postgres/lib:$LD_LIBRARY_PATH
   ```

## Additional Resources

- **YugabyteDB Documentation**: https://docs.yugabyte.com/
- **Build and Test Guide**: docs/content/stable/contribute/core-database/build-and-test.md
- **Architecture Documentation**: architecture/design/
- **Community Forum**: https://forum.yugabyte.com/
- **GitHub Issues**: https://github.com/yugabyte/yugabyte-db/issues

## Contributing

When contributing ppc64le-specific changes:

1. Test on actual ppc64le hardware
2. Ensure compatibility with POWER8, POWER9, and POWER10
3. Document any architecture-specific behavior
4. Follow the contribution guidelines in CONTRIBUTING.md

## Support

For ppc64le-specific issues:

1. Check existing GitHub issues with label `arch:ppc64le`
2. Post questions on the community forum
3. Contact YugabyteDB support if you have an enterprise license

---

**Last Updated**: 2026-04-09
**YugabyteDB Version**: 2025.2+
**Supported RHEL Versions**: RHEL 8, RHEL 9
**Architecture**: ppc64le (POWER8+)