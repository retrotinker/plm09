Name:           plm09
Version:        0.3.1
Release:        1%{?dist}
Summary:        PL/M-80 compiler for the 6809, with a 6809 simulator

License:        MIT
URL:            https://github.com/retrotinker/plm09
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz
# made by make rpm with tools/rpmdocs.py: the documentation's runtime paths
# (-I runtime ...) changed to %%{_datadir}/plm09/runtime, where this package
# installs the runtime
Patch0:         %{name}-%{version}-datadir.patch

BuildRequires:  gcc
BuildRequires:  make
# to rebuild the PDF manuals from the patched Markdown: the TeX packages
# xelatex reads for them (found with xelatex -recorder; pandoc's template
# skips some, such as parskip, when they aren't installed)
BuildRequires:  pandoc-cli
BuildRequires:  texlive-xetex
BuildRequires:  texlive-latex
BuildRequires:  texlive-amsfonts
BuildRequires:  texlive-amsmath
BuildRequires:  texlive-bookmark
BuildRequires:  texlive-booktabs
BuildRequires:  texlive-etoolbox
BuildRequires:  texlive-fontspec
BuildRequires:  texlive-geometry
BuildRequires:  texlive-hyperref
BuildRequires:  texlive-iftex
BuildRequires:  texlive-lm
BuildRequires:  texlive-lm-math
BuildRequires:  texlive-microtype
BuildRequires:  texlive-tools
BuildRequires:  texlive-unicode-math
BuildRequires:  texlive-url
BuildRequires:  texlive-xcolor
BuildRequires:  texlive-xurl
# for %%check
BuildRequires:  lwtools
BuildRequires:  python3
BuildRequires:  groff-base
# plmc's output is assembled with lwasm
Requires:       lwtools

%description
plm09 is a compiler for PL/M-80, plmc, that emits Motorola 6809 assembly
language for lwasm, together with the startup code, runtime routines and
hooks for several targets (the sim6809 simulator, the Tandy Color Computer,
OS-9 and NitrOS-9, FLEX 9 and small 6809 boards).

It also includes sim6809, a headless MC6809 simulator with OS-9 and FLEX
environments and serial devices, and srec2flex, which converts S-records
to FLEX binary (.CMD) files.

%prep
%autosetup -p1
# the patch must name the directory %%install puts the runtime in
grep -q -- '%{_datadir}/plm09/runtime/sim' docs/man/plmc.1

%build
%set_build_flags
%make_build WERROR=
# the PDF manuals, from the patched Markdown
make -B docs

%install
%make_install PREFIX=%{_prefix} BINDIR=%{_bindir} MANDIR=%{_mandir} DATADIR=%{_datadir} WERROR=

%check
# the suites that need nothing beyond lwasm, python3 and groff
make WERROR= sim-test man-test lex-test parse-test err-test run-test fuzz-test

%files
%license LICENSE
%doc README.md docs/plmc.pdf docs/sim6809.pdf
%{_bindir}/plmc
%{_bindir}/sim6809
%{_bindir}/srec2flex
%{_mandir}/man1/plmc.1*
%{_mandir}/man1/sim6809.1*
%{_mandir}/man1/srec2flex.1*
%{_datadir}/%{name}/

%changelog
* Wed Oct 28 2026 John W. Linville <linville@tuxdriver.com> - 0.3.1-1
- Ignore compiler controls other than INCLUDE, with a warning

* Wed Oct 28 2026 John W. Linville <linville@tuxdriver.com> - 0.3.0-1
- Add the $INCLUDE compiler control and plmc's -I option

* Wed Oct 28 2026 John W. Linville <linville@tuxdriver.com> - 0.2.0-3
- Source0 is the GitHub release archive

* Tue Oct 27 2026 John W. Linville <linville@tuxdriver.com> - 0.2.0-2
- Add the URL of the GitHub repository

* Tue Oct 27 2026 John W. Linville <linville@tuxdriver.com> - 0.2.0-1
- Remove INPUT and OUTPUT: the 6809 has no I/O ports; use BYTE variables
  AT the device's address instead
- RT_PORTS is no longer defined by the hooks
* Mon Oct 26 2026 John W. Linville <linville@tuxdriver.com> - 0.1.0-2
- Patch the documentation's runtime paths to the installed directory
- Rebuild the PDF manuals from the patched Markdown
* Mon Oct 26 2026 John W. Linville <linville@tuxdriver.com> - 0.1.0-1
- First package
