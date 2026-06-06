.PHONY: duckdb install-duckdb clean-duckdb clean-all lintcheck check-regression-duckdb clean-regression

MODULE_big = pg_duckdb
EXTENSION = pg_duckdb
DATA = pg_duckdb.control $(wildcard sql/pg_duckdb--*.sql)

SRCS = $(wildcard src/*.cpp src/*/*.cpp)
OBJS = $(subst .cpp,.o, $(SRCS))

C_SRCS = $(wildcard src/*.c src/*/*.c)
OBJS += $(subst .c,.o, $(C_SRCS))

# set to `make` to disable ninja
DUCKDB_GEN ?= ninja
# used to know what version of extensions to download
DUCKDB_VERSION = v1.4.3
# duckdb build tweaks
DUCKDB_CMAKE_VARS = -DCXX_EXTRA=-fvisibility=default -DBUILD_SHELL=0 -DBUILD_PYTHON=0 -DBUILD_UNITTESTS=0
# set to 1 to disable asserts in DuckDB. This is particularly useful in combinition with MotherDuck.
# When asserts are enabled the released motherduck extension will fail some of
# those asserts. By disabling asserts it's possible to run a debug build of
# DuckDB agains the release build of MotherDuck.
DUCKDB_DISABLE_ASSERTIONS ?= 0

DUCKDB_BUILD_CXX_FLAGS=
DUCKDB_BUILD_TYPE=
ifeq ($(DUCKDB_BUILD), Debug)
	DUCKDB_BUILD_CXX_FLAGS = -g -O0 -D_GLIBCXX_ASSERTIONS
	DUCKDB_BUILD_TYPE = debug
	DUCKDB_MAKE_TARGET = debug
else ifeq ($(DUCKDB_BUILD), ReleaseStatic)
	DUCKDB_BUILD_CXX_FLAGS =
	DUCKDB_BUILD_TYPE = release
	DUCKDB_MAKE_TARGET = bundle-library
else
	DUCKDB_BUILD_CXX_FLAGS =
	DUCKDB_BUILD_TYPE = release
	DUCKDB_MAKE_TARGET = release
endif

DUCKDB_BUILD_DIR = third_party/duckdb/build/$(DUCKDB_BUILD_TYPE)

ifeq ($(DUCKDB_BUILD), ReleaseStatic)
	FULL_DUCKDB_LIB = $(DUCKDB_BUILD_DIR)/libduckdb_bundle.a
	PG_DUCKDB_LINK_FLAGS = $(FULL_DUCKDB_LIB) -lcurl
else
	FULL_DUCKDB_LIB = $(DUCKDB_BUILD_DIR)/src/libduckdb$(DLSUFFIX)
	PG_DUCKDB_LINK_FLAGS = -lduckdb
endif


PG_DUCKDB_LINK_FLAGS += -Wl,-rpath,$(PG_LIB)/ -L$(DUCKDB_BUILD_DIR)/src -L$(PG_LIB) -lstdc++ -llz4

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

override PG_CPPFLAGS += -Iinclude -isystem third_party/duckdb/src/include -isystem third_party/duckdb/third_party/re2 -isystem $(INCLUDEDIR_SERVER) ${COMPILER_FLAGS}
override PG_CXXFLAGS += -std=c++17 ${DUCKDB_BUILD_CXX_FLAGS} ${COMPILER_FLAGS} -Wno-register -Weffc++
# Ignore declaration-after-statement warnings in our code. Postgres enforces
# this because their ancient style guide requires it, but we don't care. It
# would only apply to C files anyway, and we don't have many of those. The only
# ones that we do have are vendored in from Postgres (ruleutils), and allowing
# declarations to be anywhere is even a good thing for those as we can keep our
# changes to the vendored code in one place.
override PG_CFLAGS += -Wno-declaration-after-statement

SHLIB_LINK += $(PG_DUCKDB_LINK_FLAGS)

include Makefile.global

# We need the DuckDB header files to build our own .o files. We depend on the
# duckdb submodule HEAD, because that target pulls in the submodule which
# includes those header files. This does mean that we rebuild our .o files
# whenever we change the DuckDB version, but that seems like a fairly
# reasonable thing to do anyway, even if not always strictly necessary always.
$(OBJS): .git/modules/third_party/duckdb/HEAD

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

.git/modules/third_party/duckdb/HEAD:
	git submodule update --init --recursive

$(FULL_DUCKDB_LIB): .git/modules/third_party/duckdb/HEAD third_party/pg_duckdb_extensions.cmake
ifeq ($(DUCKDB_BUILD), ReleaseStatic)
	mkdir -p third_party/duckdb/build/release/vcpkg_installed
endif
	OVERRIDE_GIT_DESCRIBE=$(DUCKDB_VERSION) \
	GEN=$(DUCKDB_GEN) \
	CMAKE_VARS="$(DUCKDB_CMAKE_VARS)" \
	DISABLE_SANITIZER=1 \
	DISABLE_ASSERTIONS=$(DUCKDB_DISABLE_ASSERTIONS) \
	EXTENSION_CONFIGS="../pg_duckdb_extensions.cmake" \
	$(MAKE) -C third_party/duckdb \
	$(DUCKDB_MAKE_TARGET)

ifeq ($(DUCKDB_BUILD), ReleaseStatic)
install-duckdb: $(FULL_DUCKDB_LIB) $(shlib)
else
install-duckdb: $(FULL_DUCKDB_LIB) $(shlib)
	$(install_bin) -m 755 $(FULL_DUCKDB_LIB) $(DESTDIR)$(PG_LIB)
endif

clean-duckdb:
	rm -rf third_party/duckdb/build

install: install-duckdb

clean-all: clean clean-regression clean-duckdb

lintcheck:
	clang-tidy $(SRCS) -- -I$(INCLUDEDIR) -I$(INCLUDEDIR_SERVER) -Iinclude $(CPPFLAGS) -std=c++17
	ruff check

format:
	find src include -iname '*.hpp' -o -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' | xargs git clang-format origin/main
	ruff format

format-all:
	find src include -iname '*.hpp' -o -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' | xargs clang-format -i
	ruff format
