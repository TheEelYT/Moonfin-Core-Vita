"""Run actual Borealis focus/clear methods with lightweight UI stubs.

This tests ownership and focus, not the Vita renderer. The original dependency
must fail the same checks, demonstrating the stale-login-control regression.
"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

VITA = Path(__file__).parents[1]
SOURCE = VITA / "vendor/borealis/library/lib/core/application.cpp"
SPEC = importlib.util.spec_from_file_location("prepare_borealis", VITA / "scripts/prepare-borealis.py")
patcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patcher)


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <iostream>
#include <stdexcept>
#include <vector>
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct View {
    bool focusable = true;
    int gained = 0, lost = 0;
    View* getDefaultFocus() { return focusable ? this : nullptr; }
    void onFocusGained() { ++gained; }
    void onFocusLost() { ++lost; }
    const char* describe() { return "test view"; }
};
struct Event { int fired = 0; template<class... T> void fire(T...) { ++fired; } };
struct Logger { template<class... T> static void debug(T...) {} };
struct Activity;
struct Application {
    inline static View *currentFocus = nullptr, *repetitionOldFocus = nullptr;
    inline static Event globalFocusChangeEvent, globalHintsUpdateEvent;
    inline static std::vector<Activity*> activitiesStack;
    inline static std::vector<View*> focusStack;
    static void giveFocus(View*);
    static void clear();
};
struct Activity {
    View control;
    void willDisappear(bool) {
        require(!Application::currentFocus, "Focus must clear before activity teardown");
        require(control.lost == 1, "Control must receive focus-lost while alive");
    }
};
@METHODS@
int main() {
    try {
        View login;
        Application::giveFocus(&login);
        Application::giveFocus(&login);
        require(login.gained == 1, "Repeated focus must be harmless");
        Application::giveFocus(nullptr);
        require(!Application::currentFocus, "Null focus must release the old login control");
        require(login.lost == 1, "Old control must be notified");
        int events = Application::globalFocusChangeEvent.fired;
        Application::giveFocus(nullptr);
        require(Application::globalFocusChangeEvent.fired == events, "Repeated null focus must be harmless");
        View emptyHome;
        emptyHome.focusable = false;
        Application::giveFocus(&login);
        Application::giveFocus(&emptyHome);
        require(!Application::currentFocus, "Empty new screen must not retain old focus");
        View menuButton;
        Application::giveFocus(&menuButton);
        require(menuButton.gained == 1, "New menu must receive focus");
        auto* activity = new Activity();
        Application::giveFocus(&activity->control);
        Application::repetitionOldFocus = &activity->control;
        Application::focusStack.push_back(&activity->control);
        Application::activitiesStack.push_back(activity);
        Application::clear();
        require(!Application::currentFocus && !Application::repetitionOldFocus, "Deleted screen must leave no focus");
        require(Application::activitiesStack.empty() && Application::focusStack.empty(), "Stacks must clear");
        Application::clear();
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
'''


class BorealisFocusTest(unittest.TestCase):
    def exercise(self, source):
        methods = "\n".join(method(source, s) for s in (
            "void Application::giveFocus(View* view)", "void Application::clear()"))
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "focus.cpp", Path(folder) / "focus-test"
            cpp.write_text(HARNESS.replace("@METHODS@", methods))
            compiler = shutil.which("c++")
            self.assertIsNotNone(compiler, "A host C++ compiler is required")
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            return subprocess.run([str(binary)], capture_output=True, text=True)

    def test_focus_release_and_teardown(self):
        result = self.exercise(patcher.fix_focus(SOURCE.read_text()))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_original_dependency_reproduces_bug(self):
        result = self.exercise(SOURCE.read_text())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Null focus must release the old login control", result.stderr)

    def test_changed_dependency_requires_review(self):
        with self.assertRaisesRegex(RuntimeError, "context changed"):
            patcher.fix_focus("unrecognized source")


if __name__ == "__main__":
    unittest.main()
