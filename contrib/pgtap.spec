Summary:	Unit testing suite for PostgreSQL
Name:		pgtap
Version:	0.25
Release:	2%{?dist}
Group:		Applications/Databases
License:	BSD
URL:		http://pgtap.projects.postgresql.org
Source0:	ftp://ftp.postgresql.org/pub/projects/pgFoundry/pgtap/pgtap-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root
 
%description
pgTAP is a unit testing framework for PostgreSQL written in PL/pgSQL and
PL/SQL. It includes a comprehensive collection of TAP-emitting assertion
functions, as well as the ability to integrate with other TAP-emitting
test frameworks. It can also be used in the xUnit testing style.

%define postgresver %(pg_config --version|awk '{print $2}'| cut -d. -f1,2)
Requires:       postgresql-server = %{postgresver}, perl-Test-Harness >= 3.0
BuildRequires:	postgresql-devel = %{postgresver}

%if "%{postgresver}" != "8.4"
BuildArch:	noarch
%endif
 
%prep
%setup -q

%build
make USE_PGXS=1 TAPSCHEMA=tap

%install
%{__rm} -rf  %{buildroot}
make install USE_PGXS=1 DESTDIR=%{buildroot}

%clean
%{__rm} -rf  %{buildroot}

%files
%defattr(-,root,root,-)
%if "%{postgresver}" == "8.3"
%{_libdir}/pgsql/pgtap.so
%endif
%{_datadir}/pgsql/contrib/*
%{_docdir}/pgsql/contrib/README.pgtap

%changelog
* Sun Mar 01 2010 Darrell Fuhriman <darrell@renewfund.com> 0.24-2
- Make install work where the pgtap.so library is needed.

* Sun Dec 27 2009 David Wheeler <david@kineticode.com> 0.24-1
- Updated Source URL to a more predictable format.

* Mon Aug 24 2009 David Fetter <david.fetter@pgexperts.com> 0.23-1
- Got corrected .spec from Devrim GUNDUZ <devrim@gunduz.org>
- Bumped version to 0.23.

* Wed Aug 19 2009 Darrell Fuhriman <darrell@projectdx.com> 0.22-1
- initial RPM
 
