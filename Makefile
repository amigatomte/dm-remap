all:
	$(MAKE) -C src

clean:
	$(MAKE) -C src clean

install:
	$(MAKE) -C src install

# ============================================================================
# Packaging targets
# ============================================================================

# DEB package (Debian/Ubuntu)
deb:
	@echo "Building DEB package..."
	debuild -us -uc
	@echo "DEB package created in parent directory"
	@ls -lh ../*.deb 2>/dev/null || echo "Build directory: .."

deb-clean:
	@echo "Cleaning DEB build artifacts..."
	debuild clean
	rm -f ../dm-remap*.deb ../dm-remap*.dsc ../dm-remap*.changes ../dm-remap*.tar.gz

# RPM package (Red Hat/Fedora/CentOS)
rpm:
	@echo "Building RPM package..."
	@mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	rpmbuild -bb dm-remap.spec
	@echo "RPM package created in ~/rpmbuild/RPMS/"
	@ls -lh ~/rpmbuild/RPMS/*/*.rpm 2>/dev/null || echo "Build directory: ~/rpmbuild/RPMS/"

rpm-clean:
	@echo "Cleaning RPM build artifacts..."
	rm -rf ~/rpmbuild/BUILD/* ~/rpmbuild/RPMS/*

# Both DEB and RPM packages
packages: deb rpm
	@echo "All packages built successfully"

packages-clean: deb-clean rpm-clean
	@echo "All package build artifacts cleaned"

# ============================================================================
# DKMS targets (Dynamic Kernel Module Support)
# ============================================================================

# Build DKMS source package for DEB
dkms-deb:
	@echo "Building DKMS DEB package..."
	@cd debian-dkms && debuild -us -uc
	@echo "DKMS DEB package created in parent directory"
	@ls -lh ../*dkms*.deb 2>/dev/null || echo "Build directory: .."

dkms-deb-clean:
	@echo "Cleaning DKMS DEB build artifacts..."
	@cd debian-dkms && debuild clean || true
	rm -f ../*dkms*.deb ../*dkms*.dsc ../*dkms*.changes ../*dkms*.tar.gz

# Build DKMS source package for RPM
dkms-rpm:
	@echo "Building DKMS RPM package..."
	@mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	rpmbuild -bb dm-remap-dkms.spec
	@echo "DKMS RPM package created in ~/rpmbuild/RPMS/"
	@ls -lh ~/rpmbuild/RPMS/*/*.rpm 2>/dev/null | grep dkms || echo "Build directory: ~/rpmbuild/RPMS/"

dkms-rpm-clean:
	@echo "Cleaning DKMS RPM build artifacts..."
	rm -rf ~/rpmbuild/BUILD/* ~/rpmbuild/RPMS/*

# Build all DKMS packages
dkms-packages: dkms-deb dkms-rpm
	@echo "All DKMS packages built successfully"

dkms-packages-clean: dkms-deb-clean dkms-rpm-clean
	@echo "All DKMS package build artifacts cleaned"

# Show packaging and DKMS status
show-packaging:
	@echo "==========================================="
	@echo "dm-remap Packaging & DKMS Status"
	@echo "==========================================="
	@echo ""
	@echo "Traditional Packages:"
	@echo "  make deb              - Build DEB package"
	@echo "  make rpm              - Build RPM package"
	@echo "  make packages         - Build all packages"
	@echo ""
	@echo "DKMS Packages (recommended):"
	@echo "  make dkms-deb         - Build DKMS DEB package"
	@echo "  make dkms-rpm         - Build DKMS RPM package"
	@echo "  make dkms-packages    - Build all DKMS packages"
	@echo ""
	@echo "Cleanup:"
	@echo "  make deb-clean        - Clean DEB artifacts"
	@echo "  make rpm-clean        - Clean RPM artifacts"
	@echo "  make packages-clean   - Clean all package artifacts"
	@echo "  make dkms-deb-clean   - Clean DKMS DEB artifacts"
	@echo "  make dkms-rpm-clean   - Clean DKMS RPM artifacts"
	@echo "  make dkms-packages-clean - Clean all DKMS artifacts"
	@echo ""
	@echo "Package files created in:"
	@echo "  Traditional DEB: Parent directory (../*.deb)"
	@echo "  Traditional RPM: ~/rpmbuild/RPMS/"
	@echo "  DKMS DEB:       Parent directory (../*dkms*.deb)"
	@echo "  DKMS RPM:       ~/rpmbuild/RPMS/"
	@echo ""
	@echo "Installation:"
	@echo "  Traditional DEB: sudo dpkg -i ../dm-remap*.deb"
	@echo "  Traditional RPM: sudo rpm -i ~/rpmbuild/RPMS/*/*.rpm"
	@echo "  DKMS DEB:        sudo apt-get install ./dm-remap-dkms.deb"
	@echo "  DKMS RPM:        sudo dnf install dm-remap-dkms*.rpm"
	@echo ""
	@echo "Why DKMS?"
	@echo "  ✓ One package for all kernel versions"
	@echo "  ✓ Automatic rebuild on kernel updates"
	@echo "  ✓ No need to maintain per-kernel packages"
	@echo ""
	@echo "Documentation:"
	@echo "  Traditional: docs/development/PACKAGING.md"
	@echo "  DKMS:        docs/development/DKMS.md"
	@echo "==========================================="

.PHONY: all clean install deb deb-clean rpm rpm-clean packages packages-clean \
        dkms-deb dkms-deb-clean dkms-rpm dkms-rpm-clean dkms-packages dkms-packages-clean \
        show-packaging