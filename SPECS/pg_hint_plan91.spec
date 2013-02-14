# SPEC file for pg_hint_plan
# Copyright(C) 2012 NIPPON TELEGRAPH AND TELEPHONE CORPORATION

%define _pgdir   /usr/pgsql-9.1
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share

## Set general information for pg_hint_plan.
Summary:    Optimizer hint for PostgreSQL 9.1
Name:       pg_hint_plan91
Version:    1.0.0
Release:    1%{?dist}
License:    BSD
Group:      Applications/Databases
Source0:    %{name}-%{version}.tar.gz
#URL:        http://example.com/pg_hint_plan/
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)
Vendor:     NIPPON TELEGRAPH AND TELEPHONE CORPORATION

## We use postgresql-devel package
BuildRequires:  postgresql91-devel
Requires:  postgresql91-libs

## Description for "pg_hint_plan"
%description
pg_hint_plan provides capability to force arbitrary plan to PostgreSQL' planner
to optimize queries by hand directly.

If you have query plan better than which PostgreSQL chooses, you can force your
plan by adding special comment block with optimizer hint before the query you
want to optimize.  You can control scan method, join method, join order, and
planner-related GUC parameters during planning.

Note that this package is available for only PostgreSQL 9.1.

## pre work for build pg_hint_plan
%prep
%setup -q

## Set variables for build environment
%build
make %{?_smp_mflags}

## Set variables for install
%install
rm -rf %{buildroot}
install -d %{buildroot}%{_libdir}
install pg_hint_plan.so %{buildroot}%{_libdir}/pg_hint_plan.so

%clean
rm -rf %{buildroot}

%files
%defattr(0755,root,root)
%{_libdir}/pg_hint_plan.so

# History of pg_hint_plan.
%changelog
* Mon Sep 24 2012 Shigeru Hanada <shigeru.hanada@gmail.com>
- Initial cut for 1.0.0

