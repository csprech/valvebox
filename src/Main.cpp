#include "engine/Engine.h"
#include "ui/MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace valvebox
{
class ValveboxApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        engine = std::make_unique<Engine>();
        window = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        if (window != nullptr)
            if (auto* mc = dynamic_cast<ui::MainComponent*> (window->getContentComponent()))
                mc->prepareToQuit();
        window.reset();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, Engine& eng) : DocumentWindow (name, juce::Colour (0xff0d0e10), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new ui::MainComponent (eng), true);
            setResizable (true, true);
            setResizeLimits (960, 600, 3200, 2000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (auto* c = getContentComponent())
                c->grabKeyboardFocus();
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
        void activeWindowStatusChanged() override
        {
            DocumentWindow::activeWindowStatusChanged();
            if (isActiveWindow())
                if (auto* c = getContentComponent(); c != nullptr && c->isShowing() && ! c->hasKeyboardFocus (true))
                    c->grabKeyboardFocus();
        }
    };

private:
    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> window;
};
} // namespace valvebox

START_JUCE_APPLICATION (valvebox::ValveboxApplication)
