#!/usr/bin/env python3
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# the main idea is from an Hossein's script in https://gerrit.libreoffice.org/c/core/+/207697
# then I tried to improve it a bit with Chatgpt GPT-5.5

# Example to call the script
# ./bin/check-sid-slots.py SID_SVX_START

# Here's the initial Hossein's command:

r'''
git grep -h "SID_SFX_START[ ]*+" "*.hrc" | grep -Ev '^\s*//' \
     | awk -F\( {'print $2'} | awk -F\) {'print $1'}           \
     | awk '{gsub(/^ +| +$/,"")}1' | sort | uniq -c | grep "      2"
'''


import collections
import re
import subprocess
import sys


def check_sid(sid_type):
    try:
        result = subprocess.run(
            ["git", "grep", "-h", f"{sid_type}", "*.hrc"],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        print(e.stderr, file=sys.stderr)
        return 1

    slots = collections.defaultdict(list)

    for line in result.stdout.splitlines():
        # Remove C++ comments
        code = line.split("//", 1)[0].strip()

        if not code:
            continue

        # Ignore comments of this type:
        #   #   SID_SVX_START + 123
        # but keep real #define
        if re.match(r"^#\s+", code):
            continue

        # Boundaries are not slots which are used
        if "FIRSTFREE" in code:
            continue

        # Only keep :
        #   SID_SVX_START + 123
        # but not :
        #   SID_SVX_START + 123 + 1
        match = re.search(
            re.escape(sid_type) +   r"\s*\+\s*(\d+)(?!\s*\+)",
            code,
        )

        if match:
            slot = int(match.group(1))
            slots[slot].append(code)

    found = False

    for slot, lines in sorted(slots.items()):
        if len(lines) > 1:
            found = True
            print(f"Duplicate {sid_type} + {slot}:")
            for line in lines:
                print(f"  {line}")
            print()

    return 1 if found else 0

def main():
  for sid_type in sys.argv[1:]:
    check_sid(sid_type)

if __name__ == "__main__":
    sys.exit(main())

# vim: set shiftwidth=4 softtabstop=4 expandtab:
