#!/usr/bin/env python3
"""toggle debug flags in makefile"""

import re
import subprocess
import sys
from pathlib import Path

MAKEFILE_PATH = Path(__file__).parent.parent.parent / "Makefile"

def toggle_flag(flag_name, display_name):
    """toggle the specified flag in the makefile"""
    content = MAKEFILE_PATH.read_text()

    # check current state
    if re.search(rf'^CFLAGS \+= -D{flag_name}', content, re.MULTILINE):
        # currently enabled, disable it
        content = re.sub(
            rf'^CFLAGS \+= -D{flag_name}',
            rf'# CFLAGS += -D{flag_name}',
            content,
            flags=re.MULTILINE
        )
        state = "disabled"
    elif re.search(rf'^# CFLAGS \+= -D{flag_name}', content, re.MULTILINE):
        # currently disabled, enable it
        content = re.sub(
            rf'^# CFLAGS \+= -D{flag_name}',
            rf'CFLAGS += -D{flag_name}',
            content,
            flags=re.MULTILINE
        )
        state = "enabled"
    else:
        print(f"error: could not find {flag_name} flag in makefile")
        return False

    MAKEFILE_PATH.write_text(content)

    # clean build
    subprocess.run(["make", "clean", "BUILD_TYPE=RELEASE"],
                   cwd=MAKEFILE_PATH.parent,
                   capture_output=True)

    print(f"== {display_name} {state} ==\n")
    return True

if __name__ == "__main__":
    flag_map = {
        "Debug logging": ("DEBUG_LOG", "Debug logging"),
        "Time logging": ("TIME_LOGGING", "Time logging"),
        "FPS window": ("FPS_WINDOW", "FPS window")
    }

    if len(sys.argv) != 2 or sys.argv[1] not in flag_map:
        sys.exit(1)

    flag_name, display_name = flag_map[sys.argv[1]]
    toggle_flag(flag_name, display_name)
