"""Checks every tracked text file: valid UTF-8 and no control characters
other than tab, line feed and carriage return. Binary files (per
.gitattributes, e.g. *.pdf) are skipped. Exits 1 if any file fails.

Used by CI (.github/workflows/ci.yml); run locally with:
    python tools/check_text_files.py
"""

import subprocess
import sys


def tracked_text_files():
    names = subprocess.run(["git", "ls-files", "-z"], check=True, capture_output=True).stdout.split(b"\0")
    names = [n.decode("utf-8") for n in names if n]
    attrs = subprocess.run(["git", "check-attr", "-z", "binary", "--"] + names,
                           check=True, capture_output=True).stdout.split(b"\0")
    binary = {attrs[i].decode("utf-8") for i in range(0, len(attrs) - 2, 3) if attrs[i + 2] == b"set"}
    return [n for n in names if n not in binary]


def main():
    problems = 0
    files = tracked_text_files()
    for path in files:
        data = open(path, "rb").read()
        try:
            data.decode("utf-8")
        except UnicodeDecodeError as error:
            print(f"{path}: not valid UTF-8 ({error})")
            problems += 1
        controls = [i for i, byte in enumerate(data) if byte < 32 and byte not in (9, 10, 13)]
        if controls:
            line = data.count(b"\n", 0, controls[0]) + 1
            print(f"{path}:{line}: {len(controls)} control character(s), first 0x{data[controls[0]]:02x}")
            problems += 1
    print(f"checked {len(files)} text files, {problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
