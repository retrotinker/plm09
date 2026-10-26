Name:           plm09
Version:        0.1.0
Release:        1%{?dist}
Summary:        PL/M-80 compiler for the 6809, with a 6809 simulator

License:        MIT
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
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
OS-9 and NitrOS-9, FLEX 9 and small 6809 boards), installed in
%{_datadir}/%{name}/runtime.

It also includes sim6809, a headless MC6809 simulator with OS-9 and FLEX
environments and serial devices, and srec2flex, which converts S-records
to FLEX binary (.CMD) files.

%prep
%autosetup

%build
%set_build_flags
%make_build WERROR=

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
* Mon Oct 26 2026 John W. Linville <linville@tuxdriver.com> - 0.1.0-1
- First package
