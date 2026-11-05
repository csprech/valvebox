// Valvebox's side of the GPUI bridge: its panel's parts, and what makes each look different.

#include "PanelBridge.h"
#include "../src/engine/Engine.h"
#include "../src/ui/MainComponent.h"

namespace valvebox::ui
{
using panelbridge::mix;
using panelbridge::mixFloat;
using panelbridge::mixString;

struct GpuiAdapter : panelbridge::Adapter
{
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    std::unique_ptr<MainComponent> mc = std::make_unique<MainComponent> (*engine);

    GpuiAdapter() { mc->setSize (MainComponent::kWidth, MainComponent::kHeight); }
    ~GpuiAdapter() override
    {
        MainComponent::keyHook = nullptr;
        mc.reset();
        engine.reset();
    }

    juce::String title() override { return "Valvebox VB-16"; }
    juce::Component& panel() override { return *mc; }
    void prepareToQuit() override { mc->prepareToQuit(); }
    void setKeyHook (std::function<bool (int)> f) override { MainComponent::keyHook = std::move (f); }
    float outputLevel() override { return engine->getOutputLevel(); }
    void testSound (bool start) override
    {
        engine->withMachine ([start] (Machine& m) {
            if (start)
                m.selectPattern (1);
            m.press (start ? Key::PlayPause : Key::StopCancel);
            m.release (start ? Key::PlayPause : Key::StopCancel);
        });
    }

    std::vector<std::pair<juce::String, juce::Component*>> parts() override
    {
        std::vector<std::pair<juce::String, juce::Component*>> p;
        for (auto& k : mc->keys)
            p.push_back ({ "key", k.get() });
        for (auto& k : mc->knobs)
            p.push_back ({ "drag", k.get() });
        for (auto& l : mc->lamps)
            p.push_back ({ "static", l.w.get() });
        p.push_back ({ "drag", &mc->dial });
        p.push_back ({ "display", &mc->lcd });
        p.push_back ({ "drag", &mc->ribbonW });
        p.push_back ({ "drag", &mc->sliderW });
        p.push_back ({ "display", &mc->valves });
        p.push_back ({ "button", &mc->audioBtn });
        p.push_back ({ "button", &mc->helpBtn });
        return p;
    }

    uint64_t custom (juce::Component& c) override
    {
        uint64_t h = 1469598103934665603ull;
        if (auto* k = dynamic_cast<KeyWidget*> (&c))
        {
            const bool blinkShows = k->led == Led::Blink;
            return mix (mix (mix (mix (h, (uint64_t) k->led), k->latched), k->down), blinkShows ? k->blinkOn : 0);
        }
        if (auto* k = dynamic_cast<KnobWidget*> (&c))
            return mix (h, (uint64_t) k->getValue());
        if (auto* l = dynamic_cast<LampWidget*> (&c))
            return mix (mix (h, (uint64_t) l->state), l->state == Led::Blink ? l->blinkOn : 0);
        if (auto* d = dynamic_cast<DialWidget*> (&c))
            return mixFloat (h, d->angle);
        if (auto* l = dynamic_cast<LcdWidget*> (&c))
        {
            const auto& s = l->shown;
            h = mixString (mixString (h, s.param), s.value);
            h = mix (mix (h, s.blink ? l->blinkOn : 2), (uint64_t) s.statusRow);
            for (bool b : { s.patternInd, s.songInd, s.waveInd, s.knobInd, s.originalInd, s.clip })
                h = mix (h, b);
            for (bool b : s.status)
                h = mix (h, b);
            return mix (mixFloat (h, s.inputLevel, 0.02f), (uint64_t) (s.cursor + 1));
        }
        if (auto* r = dynamic_cast<RibbonWidget*> (&c))
            return mix (mixFloat (h, r->pos), r->touching);
        if (auto* s = dynamic_cast<SliderWidget*> (&c))
            return mixFloat (h, s->value);
        if (auto* v = dynamic_cast<ValveWindow*> (&c))
            return mixFloat (mixFloat (h, v->gain, 0.01f), v->lvl, 0.01f);
        if (auto* b = dynamic_cast<juce::Button*> (&c))
            return mix (h, b->isDown());
        return h;
    }
};
} // namespace valvebox::ui

std::unique_ptr<panelbridge::Adapter> panelbridge::makeAdapter() { return std::make_unique<valvebox::ui::GpuiAdapter>(); }
