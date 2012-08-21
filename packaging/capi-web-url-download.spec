%define ENABLE_DOWNLOAD_PROVIDER 1

Name:	capi-web-url-download
Summary:	CAPI for content download with web url
Version:	0.0.7
Release:	2
Group:		TO_BE_FILLED_IN
License:	TO_BE_FILLED_IN
URL:		N/A
Source0:	%{name}-%{version}.tar.gz
BuildRequires: pkgconfig(capi-base-common)
BuildRequires: pkgconfig(bundle)
BuildRequires: pkgconfig(dlog)
%if %ENABLE_DOWNLOAD_PROVIDER
BuildRequires: pkgconfig(capi-appfw-app-manager)
BuildRequires: pkgconfig(capi-appfw-application)
BuildRequires: pkgconfig(download-provider)
%else
BuildRequires: pkgconfig(libdownload-agent)
%endif
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
cmake . -DCMAKE_INSTALL_PREFIX="/"

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
/usr/lib/libcapi-web-url-download.so

%files devel
%defattr(-,root,root,-)
/usr/lib/libcapi-web-url-download.so
/usr/lib/pkgconfig/capi-web-url-download.pc
/usr/include/web/url_download.h

%changelog
* Mon Aug 16 2012 Jungki Kwak <jungki.kwak@samsung.com>
- Add new APIs for notification function
- The TC is changed due to change of url_download_start
- When unseting the callback function, the callback should be initialized although the error is happened.
- It remove the stop function when is called twice when destroying handle
- Add pc requries for app.h

* Mon Aug 08 2012 Jungki Kwak <jungki.kwak@samsung.com>
- Change requestid to INTEGER from String

* Mon Aug 06 2012 Jungki Kwak <jungki.kwak@samsung.com>
- The base model is changed to download provider daemon

