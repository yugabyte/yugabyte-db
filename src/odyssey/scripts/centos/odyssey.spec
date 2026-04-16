Name:		odyssey
Version:	1.0
Release:	1%{?dist}
Summary:	Advanced multi-threaded PostgreSQL connection pooler and request router

Group:		Applications/Network
License:	BSD 3-clause
URL:		https://github.com/yandex/odyssey
Source0:	odyssey.tar.gz

BuildRequires:	cmake
BuildRequires: openssl-devel
BuildRequires: zlib-devel
BuildRequires: gcc

%description
Advanced multi-threaded PostgreSQL connection pooler and request router

%prep
%setup -q -n odyssey


%build
cmake .
make %{?_smp_mflags}


%install
install -D -m 755 sources/odyssey $RPM_BUILD_ROOT/usr/bin/odyssey
install -D -m 644 odyssey.conf $RPM_BUILD_ROOT/etc/odyssey/odyssey.conf
install -D -m 644 scripts/systemd/odyssey.service $RPM_BUILD_ROOT/usr/lib/systemd/system/odyssey.service
install -D -m 644 scripts/systemd/odyssey@.service $RPM_BUILD_ROOT/usr/lib/systemd/system/odyssey@.service

%pre
useradd -md /usr/lib/odyssey odyssey >/dev/null 2>&1 || exit 0

%files
%attr(0755, root, root) /usr/bin/odyssey
%dir %attr(0755, root, root) /etc/odyssey/
%config(noreplace) %attr(0644, root, root) /etc/odyssey/odyssey.conf
%attr(0644, root, root) /usr/lib/systemd/system/odyssey.service
%attr(0644, root, root) /usr/lib/systemd/system/odyssey@.service


%changelog

