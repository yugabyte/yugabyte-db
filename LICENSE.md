## YugabyteDB Licensing

Source code in this repository is licensed under two different licenses depending on the component:

### Apache License 2.0

The following components are licensed under the [Apache License 2.0](licenses/APACHE-LICENSE-2.0.txt):

- **YugabyteDB Core Database** - The distributed SQL database engine
  - `src/yb/` - Core C++ storage engine (DocDB, consensus, tablets, servers)
  - `src/postgres/` - Modified PostgreSQL fork for YSQL compatibility
  - `src/odyssey/` - Connection pooling (note: has its own BSD-style license)
  - `java/` - Java client libraries
  - `python/` - Python build and test utilities
  - `build-support/` - Build scripts and tooling
  - `bin/` - Command-line utilities
  - `cmake_modules/` - CMake build configuration

### Polyform Free Trial License 1.0.0

The following components are licensed under the [Polyform Free Trial License 1.0.0](licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt):

- **YugabyteDB Anywhere (YBA)** - The management and orchestration platform
  - `managed/` - YBA backend (Scala/Java), UI (React), CLI, installers, and DevOps tooling
  - `troubleshoot/` - Troubleshooting framework

The Polyform Free Trial License permits evaluation use for up to 32 consecutive calendar days. For production use of YugabyteDB Anywhere, a commercial license is required. Contact [Yugabyte Sales](https://www.yugabyte.com/contact/) for licensing options.

### Build Artifacts

The build produces two sets of binaries corresponding to the above licenses:

1. **YugabyteDB** - Available at [download.yugabyte.com](https://download.yugabyte.com/local#linux), licensed under Apache License 2.0

2. **YugabyteDB Anywhere** - Available via [installer](https://docs.yugabyte.com/preview/yugabyte-platform/install-yugabyte-platform/install-software/installer/), licensed under Polyform Free Trial License 1.0.0

### Third-Party Licenses

Individual subdirectories may contain third-party code with their own licenses. These are documented in LICENSE files within those directories.

### License Files

- [Apache License 2.0](licenses/APACHE-LICENSE-2.0.txt)
- [Polyform Free Trial License 1.0.0](licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt)
