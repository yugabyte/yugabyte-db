# SPEC file for pg_store_plans
# Copyright(c) 2022-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION

%define _pgdir   /usr/pgsql-15
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share
%define _bcdir %{_libdir}/bitcode
%define _mybcdir %{_bcdir}/pg_hint_plan

%if "%(echo ${MAKE_ROOT})" != ""
  %define _rpmdir %(echo ${MAKE_ROOT})/RPMS
  %define _sourcedir %(echo ${MAKE_ROOT})
%endif

## Set general information for pg_store_plans.
Summary:    Optimizer hint on PostgreSQL 15
Name:       pg_hint_plan15
Version:    1.5.1
Release:    1%{?dist}
License:    BSD
Group:      Applications/Databases
Source0:    %{name}-%{version}.tar.gz
URL:        https://github.com/ossc-db/pg_hint_plan
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)
Vendor:     NIPPON TELEGRAPH AND TELEPHONE CORPORATION

## We use postgresql-devel package
BuildRequires:  postgresql15-devel
Requires:  postgresql15-server

## Description for "pg_hint_plan"
%description

pg_hint_plan provides capability to tweak execution plans to be
executed on PostgreSQL.

Note that this package is available for only PostgreSQL 15.

%package llvmjit
Requires: postgresql15-server, postgresql15-llvmjit
Requires: pg_hint_plan15 = 1.5.1
Summary:  Just-in-time compilation support for pg_hint_plan15

%description llvmjit
Just-in-time compilation support for pg_hint_plan15

## pre work for build pg_hint_plan
%prep
PATH=/usr/pgsql-15/bin:$PATH
if [ "${MAKE_ROOT}" != "" ]; then
  pushd ${MAKE_ROOT}
  make clean %{name}-%{version}.tar.gz
  popd
fi
if [ ! -d %{_rpmdir} ]; then mkdir -p %{_rpmdir}; fi
%setup -q

## Set variables for build environment
%build
PATH=/usr/pgsql-15/bin:$PATH
make USE_PGXS=1 LDFLAGS+=-Wl,--build-id %{?_smp_mflags}

## Set variables for install
%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(0755,root,root)
%{_libdir}/pg_hint_plan.so
%defattr(0644,root,root)
%{_datadir}/extension/pg_hint_plan--1.3.0.sql
%{_datadir}/extension/pg_hint_plan--1.3.0--1.3.1.sql
%{_datadir}/extension/pg_hint_plan--1.3.1--1.3.2.sql
%{_datadir}/extension/pg_hint_plan--1.3.2--1.3.3.sql
%{_datadir}/extension/pg_hint_plan--1.3.3--1.3.4.sql
%{_datadir}/extension/pg_hint_plan--1.3.4--1.3.5.sql
%{_datadir}/extension/pg_hint_plan--1.3.5--1.3.6.sql
%{_datadir}/extension/pg_hint_plan--1.3.6--1.3.7.sql
%{_datadir}/extension/pg_hint_plan--1.3.7--1.3.8.sql
%{_datadir}/extension/pg_hint_plan--1.3.8--1.3.9.sql
%{_datadir}/extension/pg_hint_plan--1.3.9--1.4.sql
%{_datadir}/extension/pg_hint_plan--1.4--1.4.1.sql
%{_datadir}/extension/pg_hint_plan--1.4.1--1.4.2.sql
%{_datadir}/extension/pg_hint_plan--1.4.2--1.5.sql
%{_datadir}/extension/pg_hint_plan--1.5--1.5.1.sql
%{_datadir}/extension/pg_hint_plan.control

%files llvmjit
%defattr(0755,root,root)
%{_mybcdir}
%defattr(0644,root,root)
%{_bcdir}/pg_hint_plan.index.bc
%{_mybcdir}/pg_hint_plan.bc

# History of pg_hint_plan.
%changelog
* Tue Aug 29 2023 Michael Paquier
- Version 1.5.1.
* Fri Jan 20 2023 Michael Paquier
- Version 1.5.0.
* Thu Sep 29 2022 Masahiro Ikeda
- Support PostgreSQL 15.
