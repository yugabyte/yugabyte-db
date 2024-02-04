Summary:	  Unit testing suite for PostgreSQL
Name:		    pgtap
Version:	  1.3.3
Release:	  1%{?dist}
Group:		  Applications/Databases
License:	  PostgreSQL
URL:		    https://pgtap.org/
Source0:    https://api.pgxn.org/dist/pgtap/%{version}/pgtap-%{version}.zip
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root
Provides:   %{name}
Provides:   %{name}-core
Provides:   %{name}-schema

%description
pgTAP is a unit testing framework for PostgreSQL written in PL/pgSQL and
PL/SQL. It includes a comprehensive collection of TAP-emitting assertion
functions, as well as the ability to integrate with other TAP-emitting test
frameworks. It can also be used in the xUnit testing style.

%define postgresver %(pg_config --version|awk '{print $2}'| cut -d. -f1,2)
Requires:       postgresql-server = %{postgresver}, perl-Test-Harness >= 3.0
Requires:       perl(TAP::Parser::SourceHandler::pgTAP)
BuildRequires:	postgresql-devel = %{postgresver}

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
%{_datadir}/pgsql/contrib/*
%{_docdir}/pgsql/contrib/README.pgtap

%changelog
* Sun Feb 4 2024 David E. Wheeler <david@justatheory.com> 1.3.2-1
- Update to 1.3.2

* Sun Sep 24 2023 David E. Wheeler <david@justatheory.com> 1.3.1-1
- Update to 1.3.1

* Mon Aug 14 2023 David E. Wheeler <david@justatheory.com> 1.3.0-1
- Update to 1.3.0

* Sun Dec 5 2021 David E. Wheeler <david@justatheory.com> 1.2.0-1
- Update to 1.2.0

* Sat Oct 24 2020 Jim Nasby <nasbyj@amazon.com> 1.1.0-2
- Remove last vestiges of pre-PostgreSQL 9.1
- Use https for URLs

* Mon Nov 25 2019 Jim Nasby <nasbyj@amazon.com> 1.1.0-1
- Update to 1.1.0
- Remove support for PostgreSQL prior to 9.1

* Thu Feb 21 2019 Jim Nasby <jim@nasby.net> 1.0.0-1
- Update to 1.0.0

* Sun Sep 16 2018 David E. Wheeler <david@justatheory.com> 0.99.0-1
- Update to v0.99.0.

* Mon Nov 6 2017 David E. Wheeler <david@justatheory.com> 0.98.0-1
- Update to v0.98.0.
- Added pgTAP harness Perl module requirement.
- Added explicit list of provided features.

* Mon Jan 28 2013 David Wheeler <david@justatheory.com> 0.93.0
- Upgraded to pgTAP 0.93.0

* Tue Jan 15 2013 David E. Wheeler <david@justatheory.com> 0.92.0-1
- Upgraded to pgTAP 0.92.0

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

