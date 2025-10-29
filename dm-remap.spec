Name:           dm-remap
Version:        1.0.0
Release:        1%{?dist}
Summary:        Device Mapper Remapping Target - Unlimited block remapping with O(1) performance

License:        GPLv2
URL:            https://github.com/amigatomte/dm-remap
Source0:        https://github.com/amigatomte/dm-remap/archive/v%{version}.tar.gz

BuildRequires:  kernel-devel
BuildRequires:  gcc
BuildRequires:  make
Requires:       kernel >= 5.0
Requires:       device-mapper
Requires:       device-mapper-libs

%description
dm-remap is a Linux kernel device-mapper target that provides transparent
block remapping from a primary device to a spare device. It enables:

- Unlimited remaps (4.2+ billion, up from 16,384)
- O(1) performance (~8 microsecond lookups)
- Automatic scaling (hash table resizes dynamically)
- Transparent operation (no application changes needed)
- Production ready (fully validated and tested)

%package devel
Summary:        Development files for dm-remap
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       kernel-devel

%description devel
This package contains the development headers for dm-remap.

%package doc
Summary:        Documentation for dm-remap
BuildArch:      noarch

%description doc
This package contains comprehensive documentation for dm-remap including:

- User guides and quick start tutorials
- Installation and configuration guides
- Build system documentation
- Architecture and implementation details
- API reference and troubleshooting guides

%prep
%setup -q

%build
make -C src KDIR=/lib/modules/$(uname -r)/build

%install
mkdir -p %{buildroot}/lib/modules/$(uname -r)/extra
install -D -m 0644 src/dm-remap.ko %{buildroot}/lib/modules/$(uname -r)/extra/
install -D -m 0644 src/dm-remap-test.ko %{buildroot}/lib/modules/$(uname -r)/extra/

# Install headers
mkdir -p %{buildroot}%{_includedir}/dm-remap
install -D -m 0644 include/dm-remap-v4-*.h %{buildroot}%{_includedir}/dm-remap/

# Install documentation
mkdir -p %{buildroot}%{_docdir}/%{name}
install -D -m 0644 README.md %{buildroot}%{_docdir}/%{name}/
install -D -m 0644 LICENSE %{buildroot}%{_docdir}/%{name}/
cp -r docs/* %{buildroot}%{_docdir}/%{name}/

# Install depmod configuration
mkdir -p %{buildroot}%{_sysconfdir}/depmod.d
echo "extra dm-remap" > %{buildroot}%{_sysconfdir}/depmod.d/dm-remap.conf

%post
/sbin/depmod -a %{?2} || :
echo "dm-remap kernel module installed successfully"
echo ""
echo "To load the module, run:"
echo "  sudo modprobe dm_remap"

%preun
# Try to unload module on removal
/sbin/modprobe -r dm_remap 2>/dev/null || :

%files
/lib/modules/*/extra/dm-remap.ko
/lib/modules/*/extra/dm-remap-test.ko
%{_sysconfdir}/depmod.d/dm-remap.conf

%files devel
%{_includedir}/dm-remap/

%files doc
%{_docdir}/%{name}/

%changelog
* Wed Oct 29 2025 Christian <amigatomte@example.com> - 1.0.0-1
- Initial release (pre-release v1.0)
- Unlimited remap capacity (4.2+ billion)
- O(1) lookup performance (~8 microseconds)
- Dynamic hash table resizing
- Persistent metadata management
- Comprehensive testing and validation
- Flexible build system (INTEGRATED and MODULAR modes)
- Complete documentation suite
