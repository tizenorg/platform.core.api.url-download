
Name:	capi-web-url-download
Summary:	CAPI for content download with web url
Version:	1.0.2
Release:	5
Group:		Development/Libraries
License:	Apache License, Version 2.0
URL:		https://review.tizen.org/git/?p=platform/core/api/url-download.git;a=summary
Source0:	%{name}-%{version}.tar.gz
Source1001: 	capi-web-url-download.manifest
BuildRequires: pkgconfig(capi-base-common)
BuildRequires: pkgconfig(dlog)
BuildRequires: pkgconfig(download-provider-interface)
BuildRequires: cmake

%description
CAPI for the content download

%package devel
Summary:	CAPI web url development files
Group:		Development/Libraries
Requires:	%{name} = %{version}-%{release}

%description devel
CAPI for content downloading with web url (development files)

%prep
%setup -q
cp %{SOURCE1001} .

%build
%cmake .

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install
mkdir -p %{buildroot}/usr/share/license

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/libcapi-web-url-download.so.*
/usr/share/license/%{name}

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%manifest capi-web-url-download.manifest
%{_libdir}/libcapi-web-url-download.so
%{_libdir}/pkgconfig/capi-web-url-download.pc
%{_includedir}/web/download.h
