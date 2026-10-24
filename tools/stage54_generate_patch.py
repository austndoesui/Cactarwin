#!/usr/bin/env python3
"""Generate the CACTUS-only Stage54 IOKit/BSD boundary patch after 0090.

Input is an isolated snapshot with Stage53's patch already applied;
the live Linux XNU source tree remains untouched.
"""

import argparse
import difflib
from pathlib import Path


def change(source: str, before: str, after: str, label: str) -> str:
    count = source.count(before)
    if count != 1:
        raise ValueError(f"{label}: expected unique context, found {count}")
    return source.replace(before, after, 1)


def generate(tree: Path) -> str:
    result = []
    rel = "pexpert/arm/pe_init.c"
    old = (tree / rel).read_text()
    new = change(
        old,
        "#include <pexpert/arm64/board_config.h>\n",
        "#include <pexpert/arm64/board_config.h>\n"
        "#if defined(CACTUS)\n"
        "/* The Stage53 fbcon status writer is safe before IOKit matching. */\n"
        "extern void fbcon_stage53_progress(const char *message);\n"
        "#endif\n",
        "fbcon-prototype",
    )
    new = change(
        new,
        "\tzalloc_iokit_lockdown();\n\tStartIOKitMatching();\n",
        "#if defined(CACTUS)\n"
        "\tfbcon_stage53_progress(\"ST54 zalloc_iokit_lockdown enter\");\n"
        "#endif\n"
        "\tzalloc_iokit_lockdown();\n"
        "#if defined(CACTUS)\n"
        "\tfbcon_stage53_progress(\"ST54 zalloc_iokit_lockdown done\");\n"
        "\tfbcon_stage53_progress(\"ST54 StartIOKitMatching enter\");\n"
        "#endif\n"
        "\tStartIOKitMatching();\n"
        "#if defined(CACTUS)\n"
        "\tfbcon_stage53_progress(\"ST54 StartIOKitMatching done\");\n"
        "#endif\n",
        "iokit-checkpoints",
    )
    result.append((rel, old, new))

    rel = "osfmk/kern/startup.c"
    old = (tree / rel).read_text()
    new = change(
        old,
        '\tkernel_bootstrap_log("PE_lockdown_iokit");\n'
        '\tPE_lockdown_iokit();\n',
        '\tkernel_bootstrap_log("PE_lockdown_iokit");\n'
        '#if defined(CACTUS)\n'
        '\tkernel_bootstrap_thread_log("ST54 PE_lockdown_iokit enter");\n'
        '#endif\n'
        '\tPE_lockdown_iokit();\n'
        '#if defined(CACTUS)\n'
        '\tkernel_bootstrap_thread_log("ST54 PE_lockdown_iokit done");\n'
        '#endif\n',
        "iokit-return",
    )
    new = change(
        new,
        "\tvm_set_restrictions(machine_info.max_cpus);\n",
        '#if defined(CACTUS)\n'
        '\tkernel_bootstrap_thread_log("ST54 vm_set_restrictions enter");\n'
        '\tkprintf("CACTUS54 machine_info.max_cpus=%d\\n", machine_info.max_cpus);\n'
        '#endif\n'
        '\tvm_set_restrictions(machine_info.max_cpus);\n'
        '#if defined(CACTUS)\n'
        '\tkernel_bootstrap_thread_log("ST54 vm_set_restrictions done");\n'
        '#endif\n',
        "vm-restrictions",
    )
    new = change(
        new,
        '\tkernel_bootstrap_thread_log("bsd_init enter");\n',
        '#if defined(CACTUS)\n'
        '\tkernel_bootstrap_thread_log("ST54 BSD entry boundary");\n'
        '#endif\n'
        '\tkernel_bootstrap_thread_log("bsd_init enter");\n',
        "bsd-entry",
    )
    result.append((rel, old, new))

    patch = []
    for rel, old, new in result:
        patch.append(f"diff --git a/{rel} b/{rel}\n")
        patch.extend(difflib.unified_diff(
            old.splitlines(keepends=True), new.splitlines(keepends=True),
            fromfile=f"a/{rel}", tofile=f"b/{rel}",
        ))
    return "".join(patch)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    patch = generate(args.tree)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Preserve LF hunk context even when the generator runs on Windows.
    args.output.write_bytes(patch.encode("utf-8"))
    print("STAGE54_PATCH_GENERATED", args.output, len(patch))


if __name__ == "__main__":
    main()
