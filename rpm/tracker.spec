Name:       tracker
Summary:    Desktop-neutral metadata database and search tool
Version:    3.7.0
Release:    1
License:    LGPLv2+ and GPLv2+
URL:        https://gnome.pages.gitlab.gnome.org/tracker/
Source0:    %{name}-%{version}.tar.bz2
Source1:    remove-tracker2-db.sh
Patch1:     0001-Always-insert-timestamps-into-the-database-as-string.patch
Patch2:     0002-portal-Allow-D-Bus-activation-only-through-systemd.patch
Patch3:     0003-Force-Y-year-modifier-if-check-fails.patch

BuildRequires:  meson >= 0.50
BuildRequires:  vala-devel >= 0.16
BuildRequires:  gettext
BuildRequires:  pkgconfig(dbus-glib-1) >= 0.60
BuildRequires:  pkgconfig(gio-2.0) >= 2.46.0
BuildRequires:  pkgconfig(gio-unix-2.0) >= 2.46.0
BuildRequires:  pkgconfig(gmodule-2.0) >= 2.46.0
BuildRequires:  pkgconfig(gobject-2.0) >= 2.46.0
BuildRequires:  pkgconfig(gobject-introspection-1.0)
BuildRequires:  pkgconfig(glib-2.0) >= 2.46.0
BuildRequires:  pkgconfig(icu-uc)
BuildRequires:  pkgconfig(icu-i18n)
BuildRequires:  pkgconfig(libxml-2.0) >= 2.6
BuildRequires:  pkgconfig(libsoup-3.0)
BuildRequires:  pkgconfig(sqlite3) >= 3.11
BuildRequires:  pkgconfig(systemd)
BuildRequires:  pkgconfig(json-glib-1.0) >= 1.0
BuildRequires:  python3-gobject
BuildRequires:  oneshot

Requires:   systemd-user-session-targets
Requires(post):   /sbin/ldconfig
Requires(postun): /sbin/ldconfig
%{_oneshot_requires_post}

%description
Tracker is a powerful desktop-neutral first class object database,
tag/metadata database and search tool.

It consists of a common object database that allows entities to have an
almost infinite number of properties, metadata (both embedded/harvested as
well as user definable), a comprehensive database of keywords/tags and
links to other entities.

It provides additional features for file based objects including context
linking and audit trails for a file object.

Metadata indexers are provided by the tracker-miners package.

%package devel
Summary:    Development files for %{name}
Requires:   %{name} = %{version}-%{release}

%description devel
Development files for %{name}.

%prep
%autosetup -p1 -n %{name}-%{version}/upstream

%build
%meson -Dman=false -Ddocs=false \
       -Davahi=disabled \
       -Dstemmer=disabled \
       -Dunicode_support=icu \
       -Dbash_completion=false \
       -Dsoup=soup3 \
       -Dsystemd_user_services_dir=%{_userunitdir}

%meson_build

%install
%meson_install

install -D -m 755 %{SOURCE1} %{buildroot}/%{_oneshotdir}/remove-tracker2-db.sh

%find_lang tracker3

%post
/sbin/ldconfig

if [ "$1" -ge 1 ]; then
add-oneshot --all-users remove-tracker2-db.sh || :
fi

%postun
/sbin/ldconfig

%files -f tracker3.lang
%defattr(-,root,root,-)
%license COPYING COPYING.LGPL COPYING.GPL
%{_bindir}/tracker3*
#{_libexecdir}/tracker3/
%{_libexecdir}/tracker-xdg-portal-3
%{_libdir}/libtracker-sparql-*.so.*
%{_libdir}/tracker-3.0/libtracker-http-soup3.so
%{_libdir}/tracker-3.0/libtracker-parser-libicu.so
%{_datadir}/dbus-1/services/org.freedesktop.portal.Tracker.service
%{_datadir}/tracker3/commands/
%{_userunitdir}/tracker-xdg-portal-3.service
%attr(0755, -, -) %{_oneshotdir}/remove-tracker2-db.sh

%files devel
%defattr(-,root,root,-)
%doc AUTHORS NEWS README.md
%{_includedir}/tracker-3.0/
%{_libdir}/libtracker-sparql-*.so
%{_libdir}/pkgconfig/*.pc
%dir %{_libdir}/girepository-1.0
%{_libdir}/girepository-1.0/Tracker-3.0.typelib
%dir %{_datadir}/vala
%dir %{_datadir}/vala/vapi
%{_datadir}/vala/vapi/tracker*.deps
%{_datadir}/vala/vapi/tracker*.vapi
%dir %{_datadir}/gir-1.0
%{_datadir}/gir-1.0/Tracker-3.0.gir
%{_libdir}/tracker-3.0/trackertestutils/
