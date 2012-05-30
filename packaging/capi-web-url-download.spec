
Name:	capi-web-url-download
Summary:	CAPI for content download with web url
Version:	0.0.3
Release:	1
Group:		TO_BE_FILLED_IN
License:	TO_BE_FILLED_IN
URL:		N/A
Source0:	%{name}-%{version}.tar.gz
Source1001: packaging/capi-web-url-download.manifest 
BuildRequires: pkgconfig(capi-base-common)
BuildRequires: pkgconfig(libdownload-agent)
BuildRequires: pkgconfig(bundle)
BuildRequires: pkgconfig(dlog)
BuildRequires: cmake
BuildRequires: expat-devel

%description
CAPI for the content download

%package devel
Summary:    url download
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
CAPI for content downloading with web url (developement files)

%prep
%setup -q

%build
cp %{SOURCE1001} .
cmake . -DCMAKE_INSTALL_PREFIX="/usr/lib"

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest capi-web-url-download.manifest
%defattr(-,root,root,-)
/usr/lib/libcapi-web-url-download.so

%files devel
%manifest capi-web-url-download.manifest
%defattr(-,root,root,-)
/usr/lib/libcapi-web-url-download.so
/usr/lib/pkgconfig/capi-web-url-download.pc
/usr/include/web/url_download.h

