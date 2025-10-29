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

# Show packaging status
show-packaging:
	@echo "==========================================="
	@echo "dm-remap Packaging Status"
	@echo "==========================================="
	@echo ""
	@echo "Available targets:"
	@echo "  make deb              - Build DEB package"
	@echo "  make rpm              - Build RPM package"
	@echo "  make packages         - Build all packages"
	@echo "  make deb-clean        - Clean DEB artifacts"
	@echo "  make rpm-clean        - Clean RPM artifacts"
	@echo "  make packages-clean   - Clean all artifacts"
	@echo ""
	@echo "Package files created in:"
	@echo "  DEB:  Parent directory (../*.deb)"
	@echo "  RPM:  ~/rpmbuild/RPMS/"
	@echo ""
	@echo "Installation:"
	@echo "  DEB: sudo dpkg -i ../dm-remap*.deb"
	@echo "  RPM: sudo rpm -i ~/rpmbuild/RPMS/*/*.rpm"
	@echo ""
	@echo "Documentation: docs/development/PACKAGING.md"
	@echo "==========================================="

.PHONY: all clean install deb deb-clean rpm rpm-clean packages packages-clean show-packaging.PHONY: all clean install