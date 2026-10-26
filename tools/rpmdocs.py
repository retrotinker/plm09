#!/usr/bin/env python3
"""Point the documentation at an installed runtime directory.

usage: rpmdocs.py DATADIR FILE...

Edits each Markdown or man page FILE in place, for a package that installs
the runtime in DATADIR/plm09/runtime (make install DATADIR=...):

  - an "installed-paths" block replaces the "source-paths" block after it;
    in Markdown:
        <!-- installed-paths
        text, with @RUNTIME@ for the runtime directory
        -->
        <!-- source-paths -->
        text about the source tree
        <!-- /source-paths -->
    and in a man page, the same with lines .\\" installed-paths (followed by
    the text, each line commented out with .\\" ), .\\" source-paths and
    .\\" /source-paths;
  - elsewhere, "-I runtime" and every path starting "runtime/" name the
    installed directory instead.

make rpm uses it to make the patch that plm09.spec applies.  It fails if a
file doesn't change or still has a source-tree runtime path.
"""
import re
import sys

MD_BLOCK = re.compile(
    r"<!-- installed-paths\n(.*?)-->\n<!-- source-paths -->\n.*?<!-- /source-paths -->\n",
    re.S)
ROFF_BLOCK = re.compile(
    r'^\.\\" installed-paths\n((?:\.\\" .*\n)*?)\.\\" source-paths\n.*?^\.\\" /source-paths\n',
    re.S | re.M)
# "runtime/..." not inside another path or word, and "-I runtime" alone
PATH = re.compile(r"(?<![\w/.\\-])runtime(?=/)|(?<=-I )runtime(?=[\s`])")


def convert(text, runtime, roff):
    text = PATH.sub(runtime, text)
    if roff:
        return ROFF_BLOCK.sub(
            lambda m: re.sub(r'^\.\\" ', "", m.group(1), flags=re.M)
            .replace("@RUNTIME@", runtime), text)
    return MD_BLOCK.sub(lambda m: m.group(1).replace("@RUNTIME@", runtime), text)


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: rpmdocs.py DATADIR FILE...")
    runtime = sys.argv[1].rstrip("/") + "/plm09/runtime"
    for path in sys.argv[2:]:
        with open(path) as f:
            old = f.read()
        new = convert(old, runtime, not path.endswith(".md"))
        if new == old:
            sys.exit(f"rpmdocs.py: {path}: no runtime paths changed")
        if PATH.search(new) or "source-paths" in new:
            sys.exit(f"rpmdocs.py: {path}: a source-tree runtime path is left")
        with open(path, "w") as f:
            f.write(new)


main()
