#include <juce_core/juce_core.h>
#include <iostream>

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("valvebox");

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
    }
    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
