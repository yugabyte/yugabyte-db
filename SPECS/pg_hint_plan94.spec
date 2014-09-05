# SPEC file for pg_hint_plan
# Copyright(C) 2012-2014 NIPPON TELEGRAPH AND TELEPHONE CORPORATION

%define _pgdir   /usr/pgsql-9.4
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share
%if "%(echo ${MAKE_ROOT})" != ""
  %define _rpmdir %(echo ${MAKE_ROOT})/RPMS
  %define _sourcedir %(echo ${MAKE_ROOT})
%endif

## Set general information for pg_hint_plan.
Summary:    Optimizer hint for PostgreSQL 9.4
Name:       pg_hint_plan94
Version:    1.1.1
Release:    1%{?dist}
License:    BSD
Group:      Applications/Databases
Source0:    %{name}-%{version}.tar.gz
#URL:        http://example.com/pg_hint_plan/
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)
Vendor:     NIPPON TELEGRAPH AND TELEPHONE CORPORATION

## We use postgresql-devel package
BuildRequires:  postgresql94-devel
Requires:  postgresql94-libs

## Description for "pg_hint_plan"
%description
pg_hint_plan provides capability to force arbitrary plan to PostgreSQL' planner
to optimize queries by hand directly.

If you have query plan better than which PostgreSQL chooses, you can force your
plan by adding special comment block with optimizer hint before the query you
want to optimize.  You can control scan method, join method, join order, and
planner-related GUC parameters during planning.

Note that this package is available for only PostgreSQL 9.4.

## pre work for build pg_hint_plan
%prep
PATH=/usr/pgsql-9.4/bin:$PATH
if [ "${MAKE_ROOT}" != "" ]; then
  pushd ${MAKE_ROOT}
  make clean %{name}-%{version}.tar.gz
  popd
fi
if [ ! -d %{_rpmdir} ]; then mkdir -p %{_rpmdir}; fi
%setup -q

## Set variables for build environment
%build
PATH=/usr/pgsql-9.4/bin:$PATH
make USE_PGXS=1 %{?_smp_mflags}

## Set variables for install
%install
rm -rf %{buildroot}
install -d %{buildroot}%{_libdir}
install pg_hint_plan.so %{buildroot}%{_libdir}/pg_hint_plan.so
install -d %{buildroot}%{_datadir}/extension
install -m 644 pg_hint_plan--1.1.1.sql %{buildroot}%{_datadir}/extension/pg_hint_plan--1.1.1.sql
install -m 644 pg_hint_plan.control %{buildroot}%{_datadir}/extension/pg_hint_plan.control

%clean
rm -rf %{buildroot}

%files
%defattr(0755,root,root)
%{_libdir}/pg_hint_plan.so
%defattr(0644,root,root)
%{_datadir}/extension/pg_hint_plan--1.1.1.sql
%{_datadir}/extension/pg_hint_plan.control

# History of pg_hint_plan.
%changelog
* Thu Sep 14 2014 Kyotaro Horiguchi
- Support 9.4. Bug fix. New rev 1.1.1.
* Mon Sep 02 2013 Takashi Suzuki
- Initial cut for 1.1.0
* Mon Sep 24 2012 Shigeru Hanada <shigeru.hanada@gmail.com>
- Initial cut for 1.0.0

