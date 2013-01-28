Summary:	Unit testing suite for PostgreSQL
Name:		pgtap
Version:	0.93.0
Release:	1%{?dist}
Group:		Applications/Databases
License:	PostgreSQL
URL:		http://pgtap.org/
Source0:	http://master.pgxn.org/dist/pgtap/%{version}/pgtap-%{version}.zip
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root
 
%description
pgTAP is a unit testing framework for PostgreSQL written in PL/pgSQL and
PL/SQL. It includes a comprehensive collection of TAP-emitting assertion
functions, as well as the ability to integrate with other TAP-emitting test
frameworks. It can also be used in the xUnit testing style.

%define postgresver %(pg_config --version|awk '{print $2}'| cut -d. -f1,2)
Requires:       postgresql-server = %{postgresver}, perl-Test-Harness >= 3.0
BuildRequires:	postgresql-devel = %{postgresver}

%if "%{postgresver}" != "8.4"
BuildArch:	noarch
%endif
 
%prep
%setup -q

%build
make

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
* Tue Jan 15 2013 David E. Wheeler <david@justatheory.com> 0.92.0-1
- Upgraded to pgTAP 0.92.0

* Mon Jan 28 2013 David Wheeler <david@justatheory.com> 0.93.0
- Upgraded to pgTAP 0.93.0

* Tue Aug 23 2011 David Wheeler <david@justatheory.com> 0.91.0
- Removed USE_PGXS from Makefile; it has not been supported in some time.
- Removed TAPSCHEMA from Makefile; use PGOPTIONS=--search_path=tap with
  psql instead.

* Tue Feb 01 2011 David Wheeler <david@justatheory.com> 0.25.0
- Removed pg_prove and pg_tapgen, which are now distributed via CPAN.

* Sun Mar 01 2010 Darrell Fuhriman <darrell@renewfund.com> 0.24-2
- Make install work where the pgtap.so library is needed.

* Sun Dec 27 2009 David Wheeler <david@justatheory.com> 0.24-1
- Updated Source URL to a more predictable format.

* Mon Aug 24 2009 David Fetter <david.fetter@pgexperts.com> 0.23-1
- Got corrected .spec from Devrim GUNDUZ <devrim@gunduz.org>
- Bumped version to 0.23.

* Wed Aug 19 2009 Darrell Fuhriman <darrell@projectdx.com> 0.22-1
- initial RPM

