#!/usr/bin/env python3
"""Install the built-in trace snapshot into a separate Linux kernel tree."""
import argparse
from pathlib import Path
import shutil


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kernel", type=Path, help="matching Linux 4.9 recovery source")
    args = ap.parse_args()
    root = args.kernel.resolve()
    makefile = root / "fs/pstore/Makefile"
    target = root / "fs/pstore/cactus_early_snapshot.c"
    if not makefile.is_file() or not (root / "init/main.c").is_file():
        ap.error("not a recognizable Linux kernel tree")
    text = makefile.read_text()
    directive = "obj-y += cactus_early_snapshot.o"
    source = Path(__file__).with_name("cactus_early_snapshot.c")
    if target.exists() and target.read_bytes() != source.read_bytes():
        ap.error("destination snapshot file differs; refusing to overwrite")
    shutil.copyfile(source, target)
    if directive not in text:
        makefile.write_text(text.rstrip("\n") + "\n" + directive + "\n")
    print(f"installed built-in early snapshot in {root}")


if __name__ == "__main__":
    main()
