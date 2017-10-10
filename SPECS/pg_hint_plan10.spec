# SPEC file for pg_store_plans
# Copyright(C) 2017 NIPPON TELEGRAPH AND TELEPHONE CORPORATION

%define _pgdir   /usr/pgsql-10
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share

%if "%(echo ${MAKE_ROOT})" != ""
  %define _rpmdir %(echo ${MAKE_ROOT})/RPMS
  %define _sourcedir %(echo ${MAKE_ROOT})
%endif

## Set general information for pg_store_plans.
Summary:    Optimizer hint on PostgreSQL 10
Name:       pg_hint_plan10
Version:    1.3.0
Release:    1%{?dist}
License:    BSD
Group:      Applications/Databases
Source0:    %{name}-%{version}.tar.gz
URL:        https://github.com/ossc-db/pg_hint_plan
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)
Vendor:     NIPPON TELEGRAPH AND TELEPHONE CORPORATION

## We use postgresql-devel package
BuildRequires:  postgresql10-devel
Requires:  postgresql10-libs

## Description for "pg_hint_plan"
%description

pg_hint_plan provides capability to tweak execution plans to be
executed on PostgreSQL.

Note that this package is available for only PostgreSQL 10.

## pre work for build pg_hint_plan
%prep
PATH=/usr/pgsql-10/bin:$PATH
if [ "${MAKE_ROOT}" != "" ]; then
  pushd ${MAKE_ROOT}
  make clean %{name}-%{version}.tar.gz
  popd
fi
if [ ! -d %{_rpmdir} ]; then mkdir -p %{_rpmdir}; fi
%setup -q

## Set variables for build environment
%build
PATH=/usr/pgsql-10/bin:$PATH
make USE_PGXS=1 %{?_smp_mflags}

## Set variables for install
%install
rm -rf %{buildroot}
install -d %{buildroot}%{_libdir}
install pg_hint_plan.so %{buildroot}%{_libdir}/pg_hint_plan.so
install -d %{buildroot}%{_datadir}/extension
install -m 644 pg_hint_plan--1.3.0.sql %{buildroot}%{_datadir}/extension/pg_hint_plan--1.3.0.sql
install -m 644 pg_hint_plan.control %{buildroot}%{_datadir}/extension/pg_hint_plan.control

%clean
rm -rf %{buildroot}

%files
%defattr(0755,root,root)
%{_libdir}/pg_hint_plan.so
%defattr(0644,root,root)
%{_datadir}/extension/pg_hint_plan--1.3.0.sql
%{_datadir}/extension/pg_hint_plan.control

# History of pg_hint_plan.
%changelog
* Tue Oct 10 2017 Kyotaro Horiguchi
- Support PostgreSQL 10. Version 1.3.0.
* Tue Feb 7 2017 Kyotaro Horiguchi
- Added new hint Parallel. Version 1.2.0.
* Thu Jul 21 2016 Kyotaro Horiguchi
- Support PostgreSQL 9.6
