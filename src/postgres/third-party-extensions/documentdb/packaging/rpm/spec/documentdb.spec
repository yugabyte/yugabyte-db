%global pg_version POSTGRES_VERSION
%define debug_package %{nil}

Name:           postgresql%{pg_version}-documentdb
Version:        DOCUMENTDB_VERSION
Release:        1%{?dist}
Summary:        DocumentDB is the open-source engine powering vCore-based Azure Cosmos DB for MongoDB

License:        MIT
URL:            https://github.com/microsoft/documentdb
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  postgresql%{pg_version}-devel
BuildRequires:  libicu-devel
BuildRequires:  krb5-devel
BuildRequires:  pkg-config
# The following BuildRequires are for system packages.
# Libbson and pcre2 development files are provided by scripts
# in the Dockerfile_build_rpm_packages environment, not by system RPMs.
# BuildRequires:  libbson-devel
# BuildRequires:  pcre2-devel
# BuildRequires: intel-decimal-math-devel # If a devel package exists and is used

Requires:       (postgresql%{pg_version} or percona-postgresql%{pg_version})
Requires:       (postgresql%{pg_version}-server or percona-postgresql%{pg_version}-server)
Requires:       (pgvector_%{pg_version} or percona-pgvector_%{pg_version})
Requires:       pg_cron_%{pg_version}
Requires:       postgis36_%{pg_version}
Requires:       rum_%{pg_version}
# Libbson is now bundled, so no runtime Requires for it.
# pcre2 is statically linked.
# libbid.a is bundled.

%description
DocumentDB is the open-source engine powering vCore-based Azure Cosmos DB for MongoDB. 
It offers a native implementation of document-oriented NoSQL database, enabling seamless 
CRUD operations on BSON data types within a PostgreSQL framework.

%prep
%setup -q

%build
# Keep the internal directory out of the RPM package
sed -i '/internal/d' Makefile

# Build the extension
# Ensure PG_CONFIG points to the correct pg_config for PGDG paths
make %{?_smp_mflags} PG_CONFIG=/usr/pgsql-%{pg_version}/bin/pg_config PG_CFLAGS="-std=gnu99 -Wall -Wno-error" CFLAGS=""

%install
make install DESTDIR=%{buildroot}

# Remove the bitcode directory if it's not needed in the final package
rm -rf %{buildroot}/usr/pgsql-%{pg_version}/lib/bitcode

# Bundle libbid.a (Intel Decimal Math Library static library)
# This assumes install_setup_intel_decimal_math_lib.sh has placed libbid.a
# at /usr/lib/intelmathlib/LIBRARY/libbid.a in the build environment.
mkdir -p %{buildroot}/usr/lib/intelmathlib/LIBRARY
cp /usr/lib/intelmathlib/LIBRARY/libbid.a %{buildroot}/usr/lib/intelmathlib/LIBRARY/libbid.a

# Bundle libbson shared library and pkg-config file
# These are installed by install_setup_libbson.sh into /usr (default INSTALLDESTDIR)
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_libdir}/pkgconfig

# fully versioned .so file
cp /usr/%{_lib}/libbson-1.0.so.0.0.0 %{buildroot}%{_libdir}/
# Copy the main symlinks
cp -P /usr/%{_lib}/libbson-1.0.so %{buildroot}%{_libdir}/
cp -P /usr/%{_lib}/libbson-1.0.so.0 %{buildroot}%{_libdir}/
# static library
cp /usr/%{_lib}/pkgconfig/libbson-static-1.0.pc %{buildroot}%{_libdir}/pkgconfig/

# Install source code and test files for make check
mkdir -p %{buildroot}/usr/src/documentdb
cp -r . %{buildroot}/usr/src/documentdb/
# Remove build artifacts and unnecessary files from source copy
find %{buildroot}/usr/src/documentdb -name "*.o" -delete
find %{buildroot}/usr/src/documentdb -name "*.so" -delete
find %{buildroot}/usr/src/documentdb -name "*.bc" -delete
rm -rf %{buildroot}/usr/src/documentdb/.git*
rm -rf %{buildroot}/usr/src/documentdb/build

%files
%defattr(-,root,root,-)
/usr/pgsql-%{pg_version}/lib/*.so
/usr/pgsql-%{pg_version}/share/extension/*.control
/usr/pgsql-%{pg_version}/share/extension/*.sql
/usr/src/documentdb
/usr/lib/intelmathlib/LIBRARY/libbid.a
# Bundled libbson files:
%{_libdir}/libbson-1.0.so
%{_libdir}/libbson-1.0.so.0
%{_libdir}/libbson-1.0.so.0.0.0
%{_libdir}/pkgconfig/libbson-static-1.0.pc

%changelog
* Fri Aug 29 2025 Shuai Tian <shuaitian@microsoft.com> - 0.106-0
- Add internal extension that provides extensions to the `rum` index. *[Feature]*
- Enable let support for update queries *[Feature]*. Requires `EnableVariablesSupportForWriteCommands` to be `on`.
- Enable let support for findAndModify queries *[Feature]*. Requires `EnableVariablesSupportForWriteCommands` to be `on`.
- Add internal extension that provides extensions to the `rum` index. *[Feature]*
- Optimized query for `usersInfo` command.
- Support collation with `delete` *[Feature]*. Requires `EnableCollation` to be `on`.
- Support for index hints for find/aggregate/count/distinct *[Feature]*
- Support `createRole` command *[Feature]*
- Add schema changes for Role CRUD APIs *[Feature]*
- Add support for using EntraId tokens via Plain Auth

* Mon Jul 28 2025 Shuai Tian <shuaitian@microsoft.com> - 0.105-0
- Support `$bucketAuto` aggregation stage, with granularity types: `POWERSOF2`, `1-2-5`, `R5`, `R10`, `R20`, `R40`, `R80`, `E6`, `E12`, `E24`, `E48`, `E96`, `E192` *[Feature]*
- Support `conectionStatus` command *[Feature]*.

* Mon Jun 09 2025 Shuai Tian <shuaitian@microsoft.com> - 0.104-0
- Add string case support for `$toDate` operator
- Support `sort` with collation in runtime*[Feature]*
- Support collation with `$indexOfArray` aggregation operator. *[Feature]*
- Support collation with arrays and objects comparisons *[Feature]*
- Support background index builds *[Bugfix]* (#36)
- Enable user CRUD by default *[Feature]*
- Enable let support for delete queries *[Feature]*. Requires `EnableVariablesSupportForWriteCommands` to be `on`.
- Enable rum_enable_index_scan as default on *[Perf]*
- Add public `documentdb-local` Docker image with gateway to GHCR
- Support `compact` command *[Feature]*. Requires `documentdb.enablecompact` GUC to be `on`.
- Enable role privileges for `usersInfo` command *[Feature]* 

* Fri May 09 2025 Shuai Tian <shuaitian@microsoft.com> - 0.103-0
- Support collation with aggregation and find on sharded collections *[Feature]*
- Support `$convert` on `binData` to `binData`, `string` to `binData` and `binData` to `string` (except with `format: auto`) *[Feature]*
- Fix list_databases for databases with size > 2 GB *[Bugfix]* (#119)
- Support half-precision vector indexing, vectors can have up to 4,000 dimensions *[Feature]*
- Support ARM64 architecture when building docker container *[Preview]*
- Support collation with `$documents` and `$replaceWith` stage of the aggregation pipeline *[Feature]*
- Push pg_documentdb_gw for documentdb connections *[Feature]*

* Wed Mar 26 2025 Shuai Tian <shuaitian@microsoft.com> - 0.102-0
- Support index pushdown for vector search queries *[Bugfix]*
- Support exact search for vector search queries *[Feature]*
- Inline $match with let in $lookup pipelines as JOIN Filter *[Perf]*
- Support TTL indexes *[Bugfix]* (#34)
- Support joining between postgres and documentdb tables *[Feature]* (#61)
- Support current_op command *[Feature]* (#59)
- Support for list_databases command *[Feature]* (#45)
- Disable analyze statistics for unique index uuid columns which improves resource usage *[Perf]*
- Support collation with `$expr`, `$in`, `$cmp`, `$eq`, `$ne`, `$lt`, `$lte`, `$gt`, `$gte` comparison operators (Opt-in) *[Feature]*
- Support collation in `find`, aggregation `$project`, `$redact`, `$set`, `$addFields`, `$replaceRoot` stages (Opt-in) *[Feature]*
- Support collation with `$setEquals`, `$setUnion`, `$setIntersection`, `$setDifference`, `$setIsSubset` in the aggregation pipeline (Opt-in) *[Feature]*
- Support unique index truncation by default with new operator class *[Feature]*
- Top level aggregate command `let` variables support for `$geoNear` stage *[Feature]*
- Enable Backend Command support for Statement Timeout *[Feature]*
- Support type aggregation operator `$toUUID`. *[Feature]*
- Support Partial filter pushdown for `$in` predicates *[Perf]*
- Support the $dateFromString operator with full functionality *[Feature]*
- Support extended syntax for `$getField` aggregation operator. Now the value of 'field' could be an expression that resolves to a string. *[Feature]*

* Wed Feb 12 2025 Shuai Tian <shuaitian@microsoft.com> - 0.101-0
- Push $graphlookup recursive CTE JOIN filters to index *[Perf]*
- Build pg_documentdb for PostgreSQL 17 *[Infra]* (#13)
- Enable support of currentOp aggregation stage, along with collstats, dbstats, and indexStats *[Commands]* (#52)
- Allow inlining $unwind with $lookup with `preserveNullAndEmptyArrays` *[Perf]*
- Skip loading documents if group expression is constant *[Perf]*
- Fix Merge stage not outputing to target collection *[Bugfix]* (#20)

* Thu Jan 23 2025 Shuai Tian <shuaitian@microsoft.com> - 0.100-0
- Initial Release