#!/usr/bin/python3

"""
Copyright © 2020 Zander Brown <zbrown@gnome.org>

SPDX-License-Identifier: GPL-3.0-or-later

Author: Zander Brown <zbrown@gnome.org>
"""

import re
from os import walk
from os.path import join
from sys import stderr, exit

copyright_re = re.compile("Copyright (?:\(C\)|©) (\d{4}(?:-\d{4})?) (.*)")
add_copyright_re = re.compile("(\d{4}(?:-\d{4})?) (.*)")
spdx_re = re.compile("SPDX-License-Identifier: ([\w\-\.]*)")

accepted_spdx = ["GPL-3.0-or-later"]


def fail(path: str, msg: str):
    print(f"FAIL {path}: {msg}", file=stderr)
    exit(1)


def handle_file(path: str, filename: str):
    header = []

    with open(path, "r") as src:
        # Only read the first multi-line comment, if any
        for line in src:
            if line.startswith("/*") or line.startswith(" *"):
                header.append(line[2:].strip())
            else:
                break

    header.reverse()

    if len(header) < 2:
        fail(path, "Expected header")

    line = header.pop()
    if line != "":
        fail(path, f"Unexpected content “{line}”")

    copyright_holders = []

    while header[-1].startswith("C"):
        copyright_line = header.pop()

        match = copyright_re.match(copyright_line)
        if not match:
            fail(path, f"Expected copyright line, got “{copyright_line}”")
        else:
            copyright_holders.append(match.group(2))

    if len(copyright_holders):
        # Additional copyright holders
        while add_copyright_re.match(header[-1]):
            copyright_line = header.pop()
            match = add_copyright_re.match(copyright_line)
            copyright_holders.append(match.group(2))

    copyright_holder = ", ".join(copyright_holders)

    line = header.pop()
    if line != "":
        fail(path, f"Expected blank line before SPDX, got “{line}”")

    spdx_line = header.pop()

    match = spdx_re.match(spdx_line)
    if not match:
        fail(path, f"Expected SPDX line, got “{spdx_line}")
    elif match.group(1) in accepted_spdx:
        spdx = match.group(1)
    else:
        fail(path, f"Unacceptable SPDX “{match.group(1)}”")

    print(f"{filename:>30}: {spdx:<20} {copyright_holder}")


skip_dirs = [".git", "subprojects", "contrib", "gtk-list-models"]
for root, dirs, files in walk("."):
    drop = [d for d in dirs if d.startswith("_build")]
    for d in drop:
        dirs.remove(d)
    for skip in skip_dirs:
        if skip in dirs:
            dirs.remove(skip)

    for file in files:
        path = join(root, file)
        if path.endswith(".c") or path.endswith(".h"):
            handle_file(path, file)
