#!/usr/bin/env python3
"""toggle debug logging flag in makefile"""

import re
import subprocess
from pathlib import Path

MAKEFILE_PATH = Path(__file__).parent / "Makefile"

def toggle_debug_log():
    """toggle the DEBUG_LOG flag in the makefile"""
    content = MAKEFILE_PATH.read_text()

    # check current state
    if re.search(r'^CFLAGS \+= -DDEBUG_LOG', content, re.MULTILINE):
        # currently enabled, disable it
        content = re.sub(
            r'^CFLAGS \+= -DDEBUG_LOG',
            r'# CFLAGS += -DDEBUG_LOG',
            content,
            flags=re.MULTILINE
        )
        state = "disabled"
    elif re.search(r'^# CFLAGS \+= -DDEBUG_LOG', content, re.MULTILINE):
        # currently disabled, enable it
        content = re.sub(
            r'^# CFLAGS \+= -DDEBUG_LOG',
            r'CFLAGS += -DDEBUG_LOG',
            content,
            flags=re.MULTILINE
        )
        state = "enabled"
    else:
        print("error: could not find DEBUG_LOG flag in makefile")
        return False

    MAKEFILE_PATH.write_text(content)

    # clean build
    subprocess.run(["make", "clean", "BUILD_TYPE=RELEASE"],
                   cwd=MAKEFILE_PATH.parent,
                   capture_output=True)

    print(f"== Debug logging {state} ==\n")
    return True

if __name__ == "__main__":
    toggle_debug_log()
