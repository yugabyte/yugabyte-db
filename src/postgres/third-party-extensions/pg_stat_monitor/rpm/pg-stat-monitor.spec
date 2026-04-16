%global sname percona-pg_stat_monitor
%global pgrel @@PG_REL@@
%global rpm_release @@RPM_RELEASE@@
%global pginstdir /usr/pgsql-@@PG_REL@@/

Summary:        Statistics collector for PostgreSQL
Name:           %{sname}%{pgrel}
Version:        @@VERSION@@
Release:        %{rpm_release}%{?dist}
License:        PostgreSQL
Source0:        percona-pg-stat-monitor%{pgrel}-%{version}.tar.gz
URL:            https://github.com/Percona-Lab/pg_stat_monitor
BuildRequires:  percona-postgresql%{pgrel}-devel
Requires:       postgresql-server
Provides:       percona-pg-stat-monitor%{pgrel}
Conflicts:      percona-pg-stat-monitor%{pgrel}
Obsoletes:      percona-pg-stat-monitor%{pgrel}
Epoch:          1
Packager:       Percona Development Team <https://jira.percona.com>
Vendor:         Percona, Inc

%description
The pg_stat_monitor is statistics collector tool
based on PostgreSQL's contrib module "pg_stat_statements".
.
pg_stat_monitor is developed on the basis of pg_stat_statments
as more advanced replacement for pg_stat_statment.
It provides all the features of pg_stat_statment plus its own feature set.


%prep
%setup -q -n percona-pg-stat-monitor%{pgrel}-%{version}


%build
sed -i 's:PG_CONFIG ?= pg_config:PG_CONFIG = /usr/pgsql-%{pgrel}/bin/pg_config:' Makefile
%{__make} USE_PGXS=1 %{?_smp_mflags}


%install
%{__rm} -rf %{buildroot}
%{__make} USE_PGXS=1 %{?_smp_mflags} install DESTDIR=%{buildroot}
%{__install} -d %{buildroot}%{pginstdir}/share/extension
%{__install} -m 755 README.md %{buildroot}%{pginstdir}/share/extension/README-pg_stat_monitor


%clean
%{__rm} -rf %{buildroot}


%post -p /sbin/ldconfig


%postun -p /sbin/ldconfig


%files
%defattr(755,root,root,755)
%doc %{pginstdir}/share/extension/README-pg_stat_monitor
%{pginstdir}/lib/pg_stat_monitor.so
%{pginstdir}/share/extension/pg_stat_monitor--*.sql
%{pginstdir}/share/extension/pg_stat_monitor.control
%{pginstdir}/lib/bitcode/pg_stat_monitor*.bc
%{pginstdir}/lib/bitcode/pg_stat_monitor/*.bc


%changelog
* Thu May 26 2022 Kai Wagner <kai.wagner@percona.com> - 1.0.1-1
- First patch release

* Wed Nov 17 2021 Evgeniy Patlan <evgeniy.patlan@percona.com> - 1.0.0-1
- Initial build
