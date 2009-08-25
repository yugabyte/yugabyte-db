Summary: pgtap is a unit testing suite for PostgreSQL
Name: pgtap
Version: 0.22
Release: 3.%{?dist}
Group: Applications/Databases
License: Free use, with credit
URL: http://pgtap.projects.postgresql.org
Source0: http://pgfoundry.org/frs/download.php/2316/pgtap-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: postgresql-devel
Requires: postgresql-server, perl-Test-Harness >= 3.0

%description
pgTAP is a unit testing framework for PostgreSQL written in PL/pgSQL and
PL/SQL. It includes a comprehensive collection of TAP-emitting assertion
functions, as well as the ability to integrate with other TAP-emitting
test frameworks. It can also be used in the xUnit testing style.

%prep
%setup -q


%build
make USE_PGXS=1 TAPSCHEMA=pgtap

%install
make install USE_PGXS=1  DESTDIR=%{buildroot}

%clean
%{__rm} -rf %{buildroot}


%files
%defattr(-,root,root,-)
%{_bindir}/pg_prove
%{_libdir}/pgsql/pgtap.so
%{_datadir}/pgsql/contrib/*
%{_docdir}/pgsql/contrib/README.pgtap

%changelog
* Wed Aug 19 2009 Darrell Fuhriman <darrell@projectdx.com> 0.22-1
- initial RPM
