.PHONY: duckdb install-duckdb clean-duckdb clean-all lintcheck check-regression-duckdb clean-regression

MODULE_big = pg_duckdb
EXTENSION = pg_duckdb
DATA = pg_duckdb.control $(wildcard sql/pg_duckdb--*.sql)

SRCS = $(wildcard src/*.cpp src/*/*.cpp)
OBJS = $(subst .cpp,.o, $(SRCS))

C_SRCS = $(wildcard src/*.c src/*/*.c)
OBJS += $(subst .c,.o, $(C_SRCS))

# YB: the upstream DuckDB build configuration below is intentionally disabled. Upstream compiles
# DuckDB in-tree from a git submodule and selects flags/targets per DUCKDB_BUILD, YB instead
# consumes a prebuilt static bundle produced once in the separate yugabyte-db-thirdparty repo.
# That thirdparty package is the single source of truth for the DuckDB version.

# # set to `make` to disable ninja
# DUCKDB_GEN ?= ninja
# # used to know what version of extensions to download
# DUCKDB_VERSION = v1.4.3
# # duckdb build tweaks
# DUCKDB_CMAKE_VARS = -DCXX_EXTRA=-fvisibility=default -DBUILD_SHELL=0 -DBUILD_PYTHON=0 -DBUILD_UNITTESTS=0
# # set to 1 to disable asserts in DuckDB. This is particularly useful in combinition with MotherDuck.
# # When asserts are enabled the released motherduck extension will fail some of
# # those asserts. By disabling asserts it's possible to run a debug build of
# # DuckDB agains the release build of MotherDuck.
# DUCKDB_DISABLE_ASSERTIONS ?= 0
#
# DUCKDB_BUILD_CXX_FLAGS=
# DUCKDB_BUILD_TYPE=
# ifeq ($(DUCKDB_BUILD), Debug)
# 	DUCKDB_BUILD_CXX_FLAGS = -g -O0 -D_GLIBCXX_ASSERTIONS
# 	DUCKDB_BUILD_TYPE = debug
# 	DUCKDB_MAKE_TARGET = debug
# else ifeq ($(DUCKDB_BUILD), ReleaseStatic)
# 	DUCKDB_BUILD_CXX_FLAGS =
# 	DUCKDB_BUILD_TYPE = release
# 	DUCKDB_MAKE_TARGET = bundle-library
# else
# 	DUCKDB_BUILD_CXX_FLAGS =
# 	DUCKDB_BUILD_TYPE = release
# 	DUCKDB_MAKE_TARGET = release
# endif
#
# DUCKDB_BUILD_DIR = third_party/duckdb/build/$(DUCKDB_BUILD_TYPE)
#
# ifeq ($(DUCKDB_BUILD), ReleaseStatic)
# 	FULL_DUCKDB_LIB = $(DUCKDB_BUILD_DIR)/libduckdb_bundle.a
# 	PG_DUCKDB_LINK_FLAGS = $(FULL_DUCKDB_LIB) -lcurl
# else
# 	FULL_DUCKDB_LIB = $(DUCKDB_BUILD_DIR)/src/libduckdb$(DLSUFFIX)
# 	PG_DUCKDB_LINK_FLAGS = -lduckdb
# endif
#
# PG_DUCKDB_LINK_FLAGS += -Wl,-rpath,$(PG_LIB)/ -L$(DUCKDB_BUILD_DIR)/src -L$(PG_LIB) -lstdc++ -llz4

YB_INSTRUMENTATION_TYPE = uninstrumented
ifneq (,$(filter asan%,$(YB_BUILD_TYPE)))
  YB_INSTRUMENTATION_TYPE = asan
endif
ifneq (,$(filter tsan%,$(YB_BUILD_TYPE)))
  YB_INSTRUMENTATION_TYPE = tsan
endif
YB_THIRDPARTY_INSTALLED_DIR = $(YB_THIRDPARTY_DIR)/installed/$(YB_INSTRUMENTATION_TYPE)
YB_THIRDPARTY_COMMON_DIR = $(YB_THIRDPARTY_DIR)/installed/common
YB_DUCKDB_INSTALLED_LIB_DIR = $(YB_THIRDPARTY_INSTALLED_DIR)/lib
# YB: The thirdparty duckdb package installs DuckDB's `src/include` tree under include/duckdb_internal
# (resolves duckdb.hpp and duckdb/<...>) and `third_party/re2` under include/duckdb_re2.
YB_DUCKDB_INCLUDE_DIR = $(YB_THIRDPARTY_INSTALLED_DIR)/include/duckdb_internal
YB_DUCKDB_RE2_INCLUDE_DIR = $(YB_THIRDPARTY_INSTALLED_DIR)/include/duckdb_re2
FULL_DUCKDB_LIB = $(YB_DUCKDB_INSTALLED_LIB_DIR)/libduckdb_bundle.a

# YB: Link the prebuilt static bundle. It already contains DuckDB core plus the bundled extensions
# (json, icu, httpfs); httpfs's external dependencies (curl, OpenSSL) and lz4 are resolved from the
# common thirdparty install tree.
#
# pg_duckdb's objects reference YB symbols (e.g. YBCLogImpl, pulled in via the inline
# GetMemoryChunkContext() in memutils.h). Link the YB pggate libraries so these resolve at link
# time.
PG_DUCKDB_LINK_FLAGS = $(FULL_DUCKDB_LIB) \
	-L$(YB_THIRDPARTY_COMMON_DIR)/lib -lcurl -lssl -lcrypto -llz4 \
	-L$(YB_BUILD_ROOT)/lib -lyb_pggate -lyb_pggate_util \
	-Wl,-rpath,$(PG_LIB)/ -L$(PG_LIB) -lstdc++

# Ensure -lstdc++fs is included for GCC 8 builds
CXX ?= c++
IS_GCC := $(shell $(CXX) --version 2>/dev/null | grep -q "Free Software Foundation" && echo true || echo false)
ifeq ($(IS_GCC),true)
  GCC_MAJOR := $(shell $(CXX) -dumpversion 2>/dev/null | cut -d. -f1)
  ifeq ($(GCC_MAJOR),8)
    PG_DUCKDB_LINK_FLAGS += -lstdc++fs
  endif
endif

ERROR_ON_WARNING ?=
ifeq ($(ERROR_ON_WARNING), 1)
	ERROR_ON_WARNING = -Werror
else
	ERROR_ON_WARNING =
endif

COMPILER_FLAGS=-Wno-sign-compare -Wshadow -Wswitch -Wunused-parameter -Wunreachable-code -Wno-unknown-pragmas -Wall -Wextra ${ERROR_ON_WARNING}

override PG_CPPFLAGS += -Iinclude -isystem $(YB_DUCKDB_INCLUDE_DIR) -isystem $(YB_DUCKDB_RE2_INCLUDE_DIR) -isystem $(INCLUDEDIR_SERVER) ${COMPILER_FLAGS}
# YB: C++23 so <stdatomic.h> / atomic_bool in Yugabyte postgres headers (e.g. storage/ipc.h) work when
# included from extension .cpp files; C++17 does not provide those C11 typedefs from stdatomic.h.
override PG_CXXFLAGS += -std=c++23 ${COMPILER_FLAGS} -Wno-register -Weffc++
# Ignore declaration-after-statement warnings in our code. Postgres enforces
# this because their ancient style guide requires it, but we don't care. It
# would only apply to C files anyway, and we don't have many of those. The only
# ones that we do have are vendored in from Postgres (ruleutils), and allowing
# declarations to be anywhere is even a good thing for those as we can keep our
# changes to the vendored code in one place.
override PG_CFLAGS += -Wno-declaration-after-statement

SHLIB_LINK += $(PG_DUCKDB_LINK_FLAGS)

include Makefile.global

# YB: upstream makes $(OBJS) depend on the DuckDB submodule HEAD so a DuckDB version change forces a
# recompile. YB has no submodule.
# # We need the DuckDB header files to build our own .o files. We depend on the
# # duckdb submodule HEAD, because that target pulls in the submodule which
# # includes those header files. This does mean that we rebuild our .o files
# # whenever we change the DuckDB version, but that seems like a fairly
# # reasonable thing to do anyway, even if not always strictly necessary always.
# $(OBJS): .git/modules/third_party/duckdb/HEAD

# YB: pg_duckdb's objects are compiled against DuckDB's headers and linked against the prebuilt
# bundle, both shipped together by the thirdparty "duckdb" package. Make $(OBJS) a real (not
# order-only) prerequisite of the bundle so that:
#   1. A thirdparty duckdb version bump makes the bundle newer than the objects, forcing a recompile
#      against the new headers.
#   2. Under `make -j`, objects cannot start compiling before the bundle-presence check runs,
#      so a missing thirdparty package surfaces that clear error instead of a wall of
#      "duckdb.hpp: No such file or directory" compile failures.
$(OBJS): $(FULL_DUCKDB_LIB)

COMPILE.cc.bc += $(PG_CPPFLAGS)
COMPILE.cxx.bc += $(PG_CXXFLAGS)

# shlib is the final output product - make duckdb and all .o dependencies
$(shlib): $(FULL_DUCKDB_LIB) $(OBJS)

NO_INSTALLCHECK = 1

PYTEST_CONCURRENCY = auto

check-regression-duckdb:
	$(MAKE) -C test/regression check-regression-duckdb

clean-regression:
	$(MAKE) -C test/regression clean-regression

# Specify AWS_REGION to make sure test output the same thing regardless of where they are run
installcheck: all install
	AWS_REGION=us-east-1 $(MAKE) check-regression-duckdb

pycheck: all install
	LD_LIBRARY_PATH=$(PG_LIBDIR):${LD_LIBRARY_PATH} pytest -n $(PYTEST_CONCURRENCY)

check: installcheck pycheck schedulecheck

schedulecheck:
	./scripts/schedule-check.sh

duckdb: $(FULL_DUCKDB_LIB)

# YB: There is no submodule to init and no local DuckDB build/install/clean to perform.
# .git/modules/third_party/duckdb/HEAD:
# 	git submodule update --init --recursive
#
# $(FULL_DUCKDB_LIB): .git/modules/third_party/duckdb/HEAD third_party/pg_duckdb_extensions.cmake
# ifeq ($(DUCKDB_BUILD), ReleaseStatic)
# 	mkdir -p third_party/duckdb/build/release/vcpkg_installed
# endif
# 	OVERRIDE_GIT_DESCRIBE=$(DUCKDB_VERSION) \
# 	GEN=$(DUCKDB_GEN) \
# 	CMAKE_VARS="$(DUCKDB_CMAKE_VARS)" \
# 	DISABLE_SANITIZER=1 \
# 	DISABLE_ASSERTIONS=$(DUCKDB_DISABLE_ASSERTIONS) \
# 	EXTENSION_CONFIGS="../pg_duckdb_extensions.cmake" \
# 	$(MAKE) -C third_party/duckdb \
# 	$(DUCKDB_MAKE_TARGET)
#
# ifeq ($(DUCKDB_BUILD), ReleaseStatic)
# install-duckdb: $(FULL_DUCKDB_LIB) $(shlib)
# else
# install-duckdb: $(FULL_DUCKDB_LIB) $(shlib)
# 	$(install_bin) -m 755 $(FULL_DUCKDB_LIB) $(DESTDIR)$(PG_LIB)
# endif

# YB: instead of building the bundle, verify the thirdparty "duckdb" package is present. If it is
# missing, fail with a clear pointer rather than silently erroring later. The package ships the
# bundle and headers together, so also report the header dir.
$(FULL_DUCKDB_LIB):
	@echo "ERROR: prebuilt DuckDB bundle not found at $(FULL_DUCKDB_LIB)." >&2
	@test -d $(YB_DUCKDB_INCLUDE_DIR) || \
		echo "       DuckDB headers are also missing at $(YB_DUCKDB_INCLUDE_DIR)." >&2
	@echo "       Build or sync the yugabyte-db-thirdparty 'duckdb' package first." >&2
	@exit 1

# YB: the bundle is installed by the thirdparty build, not here; nothing to install locally.
install-duckdb:

# YB: nothing to clean locally; DuckDB build artifacts live in the thirdparty tree.
clean-duckdb:
#	rm -rf third_party/duckdb/build

install: install-duckdb

clean-all: clean clean-regression clean-duckdb

lintcheck:
	# YB: -std=c++23 to match the build (see PG_CXXFLAGS above).
	clang-tidy $(SRCS) -- -I$(INCLUDEDIR) -I$(INCLUDEDIR_SERVER) -Iinclude $(CPPFLAGS) -std=c++23
	ruff check

format:
	find src include -iname '*.hpp' -o -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' | xargs git clang-format origin/main
	ruff format

format-all:
	find src include -iname '*.hpp' -o -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' | xargs clang-format -i
	ruff format
