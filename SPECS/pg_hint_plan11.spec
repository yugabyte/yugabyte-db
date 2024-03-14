# SPEC file for pg_hint_plan
# Copyright(C) 2020 NIPPON TELEGRAPH AND TELEPHONE CORPORATION

%define _pgdir   /usr/pgsql-11
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share
%define _bcdir %{_libdir}/bitcode
%define _mybcdir %{_bcdir}/pg_hint_plan

%if "%(echo ${MAKE_ROOT})" != ""
  %define _rpmdir %(echo ${MAKE_ROOT})/RPMS
  %define _sourcedir %(echo ${MAKE_ROOT})
%endif

## Set general information for pg_hint_plan.
Summary:    Optimizer hint on PostgreSQL 11
Name:       pg_hint_plan11
Version:    1.3.7
Release:    1%{?dist}
License:    BSD
Group:      Applications/Databases
Source0:    %{name}-%{version}.tar.gz
URL:        https://github.com/ossc-db/pg_hint_plan
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)
Vendor:     NIPPON TELEGRAPH AND TELEPHONE CORPORATION

## We use postgresql-devel package
BuildRequires:  postgresql11-devel
Requires:  postgresql11-server

## Description for "pg_hint_plan"
%description

pg_hint_plan provides capability to tweak execution plans to be
executed on PostgreSQL.

Note that this package is available for only PostgreSQL 11.

%package llvmjit
Requires: postgresql11-server, postgresql11-llvmjit
Requires: pg_hint_plan11 = 1.3.7
Summary:  Just-in-time compilation support for pg_hint_plan11

%description llvmjit
Just-in-time compilation support for pg_hint_plan11

## pre work for build pg_hint_plan
%prep
PATH=/usr/pgsql-11/bin:$PATH
if [ "${MAKE_ROOT}" != "" ]; then
  pushd ${MAKE_ROOT}
  make clean %{name}-%{version}.tar.gz
  popd
fi
if [ ! -d %{_rpmdir} ]; then mkdir -p %{_rpmdir}; fi
%setup -q

## Set variables for build environment
%build
PATH=/usr/pgsql-11/bin:$PATH
make USE_PGXS=1 %{?_smp_mflags}

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
%{_datadir}/extension/pg_hint_plan--1.3.2--1.3.3.sql
%{_datadir}/extension/pg_hint_plan--1.3.3--1.3.4.sql
%{_datadir}/extension/pg_hint_plan--1.3.4--1.3.5.sql
%{_datadir}/extension/pg_hint_plan--1.3.5--1.3.6.sql
%{_datadir}/extension/pg_hint_plan--1.3.6--1.3.7.sql
%{_datadir}/extension/pg_hint_plan--1.3.7.sql
%{_datadir}/extension/pg_hint_plan.control

%files llvmjit
%defattr(0755,root,root)
%{_mybcdir}
%defattr(0644,root,root)
%{_bcdir}/pg_hint_plan.index.bc
%{_mybcdir}/pg_hint_plan.bc

# History of pg_hint_plan.
%changelog
* Thu Oct 29 2020 Kyotaro Horiguchi
- Fix a bug. Version 1.3.7.
* Wed Aug 5 2020 Kyotaro Horiguchi
- Fix some bugs. Version 1.3.6.
* Thu Feb 21 2020 Kyotaro Horiguchi
- Fix some bugs. Version 1.3.5.
* Thu Jan 17 2019 Kyotaro Horiguchi
- Fix some bugs. Version 1.3.4.
* Tue Nov 13 2018 Kyotaro Horiguchi
- Support PostgreSQL 11. Version 1.3.2.
* Fri Jun 08 2018 Kyotaro Horiguchi
- Fixed a crash bug. Version 1.3.1.
* Tue Oct 10 2017 Kyotaro Horiguchi
- Support PostgreSQL 10. Version 1.3.0.
* Tue Feb 7 2017 Kyotaro Horiguchi
- Added new hint Parallel. Version 1.2.0.
* Thu Jul 21 2016 Kyotaro Horiguchi
- Support PostgreSQL 9.6
