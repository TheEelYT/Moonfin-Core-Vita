#!/usr/bin/env python3
"""Generate the Vita's Borealis source override without changing the submodule."""
from pathlib import Path
import sys


def fix_focus(source):
    changes = [
        ("    if (oldFocus != newFocus && newFocus != nullptr)",
         "    if (oldFocus != newFocus)"),
        ("void Application::clear()\n{\n",
         "void Application::clear()\n{\n"
         "    // Release focus while its view and parent chain are still alive.\n"
         "    Application::giveFocus(nullptr);\n"
         "    Application::repetitionOldFocus = nullptr;\n\n"),
    ]
    for old, new in changes:
        if source.count(old) != 1:
            raise RuntimeError("Borealis focus patch context changed; review the pinned dependency")
        source = source.replace(old, new, 1)
    return source


if __name__ == "__main__":
    source, destination = map(Path, sys.argv[1:])
    result = fix_focus(source.read_text())
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text() != result:
        destination.write_text(result)
