#!/usr/bin/env python3
#
# Copyright (C) 2026 The Phosh developers
# SPDX-License-Identifier: GPL-3.0-or-later
# Author: Gotam Gorabh <gautamy672@gmail.com>
#
# Checks for unwanted types like `gchar`, `gfloat`, `gint`. (default: 'gchar').
#
#  Usage:
#   PHOSH_BANNED_TYPES="gchar gint gfloat" \
#   PHOSH_EXCLUDE_DIRS="contrib subprojects" \
#   ./g-style.py [--sha SHA]
#
# Defaults:
#   sha = "HEAD^"

import argparse
import os
import re
import subprocess
import sys


def run_diff(sha, exclude_dirs):
    """Return diff output lines between sha and HEAD."""

    cmd = ["git", "diff", "-U0", sha, "HEAD", "--", "*.c", "*.h"]

    # Append directory exclusions
    for d in exclude_dirs:
        cmd.append(f":(exclude){d}")

    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8"
    )

    if proc.returncode != 0:
        print(f"Error running git diff: {proc.stdout}", file=sys.stderr)
        return []

    return proc.stdout.splitlines()


def find_hunks_with_lines(diff):
    file_entry_re = re.compile(r"^\+\+\+ b/(.*)$")
    diff_hunk_re = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))?")
    diff_git_re = re.compile(r"^diff --git")

    file = None
    hunks = []
    current_hunk = None

    for line in diff:

        # Detect new file (stop collecting from previous file)
        if diff_git_re.match(line):
            current_hunk = None
            continue

        # Detect file entry
        match = file_entry_re.match(line)
        if match:
            file = match.group(1)
            continue

        # Detect start of diff hunk @@
        match = diff_hunk_re.match(line)
        if match:
            start = int(match.group(1))
            length = int(match.group(2)) if match.group(2) else 1
            end = start + length

            # Create new hunk and append immediately
            current_hunk = {
                "file": file,
                "start": start,
                "end": end,
                "lines": []
            }
            hunks.append(current_hunk)
            continue

        # Collect lines inside a hunk (exclude file headers +++ and ---)
        if current_hunk:
            if (line.startswith("+") and not line.startswith("+++")) or \
               (line.startswith("-") and not line.startswith("---")):
                current_hunk["lines"].append(line)

    return hunks


def scan_hunks_for_banned_types(hunks, banned_types):
    results = []

    for hunk in hunks:
        file = hunk["file"]
        line_num = hunk["start"]

        for line in hunk["lines"]:
            # Only process added lines (+), not removed lines (-)
            if line.startswith('+'):
                content = line[1:]

                for banned in banned_types:
                    if banned in content:
                        results.append((file, line_num, content))

                line_num += 1

    return results


def main(argv):
    parser = argparse.ArgumentParser(
        description="Check for unwanted types like gchar, gfloat, gint."
    )
    parser.add_argument(
        "--sha", metavar="SHA", type=str, help="SHA for the commit to compare HEAD with"
    )

    args = parser.parse_args(argv)
    sha = args.sha or "HEAD^"

    # Banned types (default: "gchar")
    banned_types = os.environ.get("PHOSH_BANNED_TYPES", "gchar").split()
    exclude_dirs = os.environ.get("PHOSH_EXCLUDE_DIRS", "").split()

    diff = run_diff(sha, exclude_dirs)
    if not diff:
        return 0

    hunks = find_hunks_with_lines(diff)
    matches = scan_hunks_for_banned_types(hunks, banned_types)

    if not matches:
        return 0

    for banned in banned_types:
        # Filter matches for this specific banned type
        filtered = [
            (file, line_no, content)
            for (file, line_no, content) in matches
            if banned in content
        ]

        if not filtered:
            # Skip printing if no match for this type
            continue

        print(f"Found '{banned}' in these files:\n")

        for file, line_no, content in filtered:
            print(f"{file}:{line_no}:{content.strip()}")

        print()
        print(f"'{banned}' usage detected. Please replace with standard C type instead.")
        print("------------------------------------------------------------")

    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
