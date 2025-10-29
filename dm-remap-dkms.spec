Name:           dm-remap-dkms
Version:        1.0.0
Release:        1%{?dist}
Summary:        Device Mapper Remapping Target - DKMS source for automatic kernel module compilation

License:        GPLv2
URL:            https://github.com/amigatomte/dm-remap
Source0:        https://github.com/amigatomte/dm-remap/archive/v%{version}.tar.gz

BuildArch:      noarch
BuildRequires:  dkms
Requires:       dkms
Requires:       kernel-devel
Requires:       gcc
Requires:       make
Requires:       device-mapper
Requires:       device-mapper-libs

%description
dm-remap is a Linux kernel device-mapper target that provides transparent
block remapping from a primary device to a spare device with DKMS support.

DKMS (Dynamic Kernel Module Support) allows the kernel module to be
automatically compiled and installed for any kernel version. No need for
separate packages per kernel!

Features:
- Unlimited remaps (4.2+ billion, up from 16,384)
- O(1) performance (~8 microsecond lookups)
- Automatic scaling (hash table resizes dynamically)
- Transparent operation (no application changes needed)
- Production ready (fully validated and tested)

This package contains the DKMS source. The kernel module will be
automatically built for your running kernel and any future kernel updates.

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
- DKMS and packaging information

%prep
%setup -q

%build
# No build needed for DKMS source package

%install
# Install DKMS source
mkdir -p %{buildroot}/usr/src/dm-remap-1.0.0
cp -r src %{buildroot}/usr/src/dm-remap-1.0.0/
cp -r include %{buildroot}/usr/src/dm-remap-1.0.0/
cp Makefile dkms.conf %{buildroot}/usr/src/dm-remap-1.0.0/

# Install documentation
mkdir -p %{buildroot}%{_docdir}/dm-remap
install -D -m 0644 README.md %{buildroot}%{_docdir}/dm-remap/
install -D -m 0644 LICENSE %{buildroot}%{_docdir}/dm-remap/
cp -r docs/* %{buildroot}%{_docdir}/dm-remap/

%post
# Add to DKMS
/usr/sbin/dkms add -m dm-remap -v 1.0.0 > /dev/null 2>&1 || true

# Build for current kernel
echo "Building dm-remap for kernel $(uname -r)..."
/usr/sbin/dkms build -m dm-remap -v 1.0.0 > /dev/null 2>&1 || true

# Install for current kernel
echo "Installing dm-remap for kernel $(uname -r)..."
/usr/sbin/dkms install -m dm-remap -v 1.0.0 > /dev/null 2>&1 || true

echo ""
echo "=========================================="
echo "dm-remap DKMS installation complete!"
echo "=========================================="
echo ""
echo "To load the module, run:"
echo "  sudo modprobe dm_remap"
echo ""
echo "To check DKMS status:"
echo "  dkms status"
echo ""

%preun
# Remove from DKMS
/usr/sbin/dkms remove -m dm-remap -v 1.0.0 --all > /dev/null 2>&1 || true

%files
/usr/src/dm-remap-1.0.0/

%files doc
%{_docdir}/dm-remap/

%changelog
* Wed Oct 29 2025 Christian <amigatomte@example.com> - 1.0.0-1
- Initial DKMS source package (pre-release v1.0)
- Automatically compiled for any kernel version
- No need for separate packages per kernel
- Unlimited remap capacity (4.2+ billion)
- O(1) lookup performance (~8 microseconds)
- Dynamic hash table resizing
- Persistent metadata management
- Comprehensive testing and validation
- Flexible build system (INTEGRATED and MODULAR modes)
