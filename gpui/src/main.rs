//! A device with a GPUI front end.
//!
//! The instrument (its C++ engine, and its JUCE panel kept off-screen as the controller and as the renderer of the
//! panel's parts) sits behind the C bridge in `bridge/PanelBridge.h`. This window lays the panel out at any size,
//! composites its parts on the GPU (each drawn again only when it changes), and sends the mouse and the computer
//! keyboard to the panel, so every key, knob and screen works as in the JUCE app.

mod config;
mod ffi;

use gpui::{
    App, Application, Bounds, Context, FocusHandle, KeyDownEvent, KeyUpEvent, Modifiers, ModifiersChangedEvent, MouseButton,
    MouseDownEvent, MouseMoveEvent, MouseUpEvent, ObjectFit, RenderImage, ScrollWheelEvent, Timer, Window, WindowBounds,
    WindowOptions, div, img, prelude::*, px, rgb, size,
};
use serde::Deserialize;
use std::sync::Arc;
use std::time::Duration;

#[derive(Deserialize, Clone)]
struct Part {
    kind: String,
    #[serde(default, rename = "type")]
    type_name: String,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
}

#[derive(Deserialize)]
struct Layout {
    width: f32,
    height: f32,
    parts: Vec<Part>,
}

struct Sprite {
    signature: u64,
    scale: u32,
    image: Arc<RenderImage>,
}

struct Device {
    panel: ffi::Panel,
    layout: Layout,
    background: Option<(u32, Arc<RenderImage>)>,
    sprites: Vec<Option<Sprite>>,
    focus: FocusHandle,
    frame: (f32, f32, f32), // the panel's scale and offset in the window
    pressed: bool,
}

fn mods_of(m: &Modifiers) -> i32 {
    (m.shift as i32) | ((m.platform as i32) << 1) | ((m.alt as i32) << 2) | ((m.control as i32) << 3)
}

fn image(w: usize, h: usize, bgra: Vec<u8>) -> Arc<RenderImage> {
    let buffer = image::RgbaImage::from_raw(w as u32, h as u32, bgra).expect("a sprite's pixels");
    Arc::new(RenderImage::new(smallvec::smallvec![image::Frame::new(buffer)]))
}

impl Device {
    fn new(cx: &mut Context<Self>) -> Self {
        let panel = ffi::Panel::new();
        let layout: Layout = serde_json::from_str(&panel.layout_json()).expect("the panel's layout");
        let n = layout.parts.len();
        let this = Self {
            panel,
            layout,
            background: None,
            sprites: (0..n).map(|_| None).collect(),
            focus: cx.focus_handle(),
            frame: (1.0, 0.0, 0.0),
            pressed: false,
        };
        // the panel's lights, screens and meters move: look again 30 times a second
        cx.spawn(async move |this, cx| {
            loop {
                Timer::after(Duration::from_millis(33)).await;
                if this.update(cx, |_, cx| cx.notify()).is_err() {
                    break;
                }
            }
        })
        .detach();
        if let Ok(report) = std::env::var(config::SELFTEST_VAR) {
            cx.spawn(async move |this, cx| {
                Timer::after(Duration::from_millis(1500)).await;
                let mut lines = Vec::new();
                if std::env::var("GPUI_SELFTEST_SOUND_ONLY").is_err() {
                    let _ = this.update(cx, |this, cx| this.self_test_parts(&mut lines, cx));
                }
                let _ = this.update(cx, |this, _| this.panel.test_sound(true));
                // (the loudest moment in a second and a half: a drum dies away)
                let mut level: f32 = 0.0;
                for _ in 0..30 {
                    Timer::after(Duration::from_millis(50)).await;
                    if let Ok(l) = this.update(cx, |this, _| this.panel.output_level()) {
                        level = level.max(l);
                    }
                }
                let _ = this.update(cx, |this, cx| {
                    lines.push(format!("{}  it plays: level {level:.3}", if level > 0.01 { "ok  " } else { "FAIL" }));
                    this.panel.test_sound(false);
                    let fails = lines.iter().filter(|l| l.starts_with("FAIL")).count();
                    lines.push(String::new());
                    lines.push(if fails == 0 { "ALL PASSED".into() } else { format!("{fails} FAILED") });
                    let _ = std::fs::write(&report, lines.join("\n") + "\n");
                    cx.quit();
                });
            })
            .detach();
        }
        this
    }

    fn to_panel(&self, pos: gpui::Point<gpui::Pixels>) -> (f32, f32) {
        let (s, ox, oy) = self.frame;
        ((f32::from(pos.x) - ox) / s, (f32::from(pos.y) - oy) / s)
    }

    fn background(&mut self, scale: f32) -> Arc<RenderImage> {
        let key = (scale * 64.0).round() as u32;
        if let Some((k, img)) = &self.background {
            if *k == key {
                return img.clone();
            }
        }
        let (w, h) = ((self.layout.width * scale).ceil() as usize, (self.layout.height * scale).ceil() as usize);
        let img = image(w, h, self.panel.render_background(scale, w, h));
        self.background = Some((key, img.clone()));
        img
    }

    /// A part as it looks now: drawn again only when its signature (or the scale) changes.
    fn sprite(&mut self, i: usize, scale: f32) -> Arc<RenderImage> {
        let signature = self.panel.signature(i);
        let key = (scale * 64.0).round() as u32;
        if let Some(s) = &self.sprites[i] {
            if s.signature == signature && s.scale == key {
                return s.image.clone();
            }
        }
        let p = &self.layout.parts[i];
        let (w, h) = ((p.w * scale).ceil().max(1.0) as usize, (p.h * scale).ceil().max(1.0) as usize);
        let img = image(w, h, self.panel.render_part(i, scale, w, h));
        self.sprites[i] = Some(Sprite { signature, scale: key, image: img.clone() });
        img
    }

    /// The self-test: each key pressed and each knob or slider dragged through the window's own mouse handling, and
    /// checked to change on the panel.
    fn self_test_parts(&mut self, lines: &mut Vec<String>, _cx: &mut Context<Self>) {
        let (mut keys, mut drags) = (0, 0);
        let mut bad = Vec::new();
        for i in 0..self.layout.parts.len() {
            let p = self.layout.parts[i].clone();
            let (x, y) = (p.x + p.w * 0.5, p.y + p.h * 0.5);
            match p.kind.as_str() {
                "slide" => {
                    // (a 3-way switch: to one end, then the other; one of them is a move)
                    let before = self.panel.signature(i);
                    let mut changed = false;
                    let ends = if p.w >= p.h {
                        [(p.x + 3.0, y), (p.x + p.w - 3.0, y), (x, y)]
                    } else {
                        [(x, p.y + 3.0), (x, p.y + p.h - 3.0), (x, y)]
                    };
                    for (xx, yy) in ends {
                        self.panel.mouse(ffi::Mouse::Down, xx, yy, 0, 1);
                        self.panel.mouse(ffi::Mouse::Up, xx, yy, 0, 1);
                        changed = changed || self.panel.signature(i) != before;
                    }
                    if !changed {
                        bad.push(format!("slide {i} ({}) at ({x:.0}, {y:.0}) didn't move", p.type_name));
                    }
                    keys += 1;
                }
                "key" => {
                    let before = self.panel.signature(i);
                    self.panel.mouse(ffi::Mouse::Down, x, y, 0, 1);
                    let during = self.panel.signature(i);
                    self.panel.mouse(ffi::Mouse::Up, x, y, 0, 1);
                    // (pressed again: a latching switch goes back to how it was)
                    self.panel.mouse(ffi::Mouse::Down, x, y, 0, 1);
                    self.panel.mouse(ffi::Mouse::Up, x, y, 0, 1);
                    if during == before {
                        bad.push(format!("{} {i} ({}) at ({x:.0}, {y:.0}) didn't press", p.kind, p.type_name));
                    }
                    keys += 1;
                }
                "drag" => {
                    let before = self.panel.signature(i);
                    self.panel.mouse(ffi::Mouse::Down, x, y, 0, 1);
                    for step in 1..=8 {
                        self.panel.mouse(ffi::Mouse::Drag, x, y - step as f32 * 5.0, 0, 1);
                    }
                    let moved = self.panel.signature(i);
                    self.panel.mouse(ffi::Mouse::Up, x, y - 40.0, 0, 1);
                    // (and back down: the knob where it was)
                    self.panel.mouse(ffi::Mouse::Down, x, y, 0, 1);
                    for step in 1..=8 {
                        self.panel.mouse(ffi::Mouse::Drag, x, y + step as f32 * 5.0, 0, 1);
                    }
                    self.panel.mouse(ffi::Mouse::Up, x, y + 40.0, 0, 1);
                    if moved == before {
                        // (already at the top of its travel: try the other way)
                        self.panel.mouse(ffi::Mouse::Down, x, y, 0, 1);
                        for step in 1..=8 {
                            self.panel.mouse(ffi::Mouse::Drag, x, y + step as f32 * 5.0, 0, 1);
                        }
                        let mut back = self.panel.signature(i);
                        self.panel.mouse(ffi::Mouse::Up, x, y + 40.0, 0, 1);
                        if back == before {
                            // (a dial turned round its centre: from the right of it, upwards)
                            let rx = p.x + p.w * 0.85;
                            self.panel.mouse(ffi::Mouse::Down, rx, y, 0, 1);
                            for step in 1..=8 {
                                self.panel.mouse(ffi::Mouse::Drag, rx, y - step as f32 * 5.0, 0, 1);
                            }
                            back = self.panel.signature(i);
                            self.panel.mouse(ffi::Mouse::Up, rx, y - 40.0, 0, 1);
                        }
                        if back == before {
                            bad.push(format!("{} {i} ({}) at ({x:.0}, {y:.0}) didn't move", p.kind, p.type_name));
                        }
                    }
                    drags += 1;
                }
                _ => {}
            }
        }
        lines.push(format!("ok    {keys} keys pressed and {drags} knobs, dials and sliders dragged through the window's handlers"));
        for b in bad {
            lines.push(format!("FAIL  {b}"));
        }
    }
}

impl Render for Device {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let view = window.viewport_size();
        let (vw, vh) = (f32::from(view.width), f32::from(view.height));
        let s = (vw / self.layout.width).min(vh / self.layout.height);
        let (ox, oy) = ((vw - self.layout.width * s) * 0.5, (vh - self.layout.height * s) * 0.5);
        self.frame = (s, ox, oy);
        let dev = (s * window.scale_factor() * 16.0).round() / 16.0;
        let place = move |x: f32, y: f32, w: f32, h: f32| div().absolute().left(px(ox + x * s)).top(px(oy + y * s)).w(px(w * s)).h(px(h * s));

        let bg = self.background(dev);
        let mut root = div()
            .size_full()
            .bg(rgb(config::WINDOW_COLOUR))
            .track_focus(&self.focus)
            .on_mouse_down(MouseButton::Left, cx.listener(|this, ev: &MouseDownEvent, window, cx| {
                this.focus.focus(window);
                let (x, y) = this.to_panel(ev.position);
                this.pressed = true;
                this.panel.mouse(ffi::Mouse::Down, x, y, mods_of(&ev.modifiers), ev.click_count as i32);
                cx.notify();
            }))
            .on_mouse_move(cx.listener(|this, ev: &MouseMoveEvent, _, cx| {
                if this.pressed {
                    let (x, y) = this.to_panel(ev.position);
                    this.panel.mouse(ffi::Mouse::Drag, x, y, mods_of(&ev.modifiers), 1);
                    cx.notify();
                }
            }))
            .on_mouse_up(MouseButton::Left, cx.listener(|this, ev: &MouseUpEvent, _, cx| {
                let (x, y) = this.to_panel(ev.position);
                this.pressed = false;
                this.panel.mouse(ffi::Mouse::Up, x, y, mods_of(&ev.modifiers), ev.click_count as i32);
                cx.notify();
            }))
            .on_scroll_wheel(cx.listener(|this, ev: &ScrollWheelEvent, _, cx| {
                let (x, y) = this.to_panel(ev.position);
                let d = ev.delta.pixel_delta(px(16.0));
                // (JUCE's wheel deltas: about a tenth for a notch)
                this.panel.wheel(x, y, f32::from(d.x) / 160.0, f32::from(d.y) / 160.0, mods_of(&ev.modifiers));
                cx.notify();
            }))
            .on_key_down(cx.listener(|this, ev: &KeyDownEvent, _, cx| {
                let k = &ev.keystroke;
                if this.panel.key(&k.key, k.key_char.as_deref().unwrap_or(""), true, mods_of(&k.modifiers)) {
                    cx.stop_propagation();
                }
                cx.notify();
            }))
            .on_key_up(cx.listener(|this, ev: &KeyUpEvent, _, cx| {
                let k = &ev.keystroke;
                this.panel.key(&k.key, k.key_char.as_deref().unwrap_or(""), false, mods_of(&k.modifiers));
                cx.notify();
            }))
            .on_modifiers_changed(cx.listener(|this, ev: &ModifiersChangedEvent, _, cx| {
                this.panel.modifiers(mods_of(&ev.modifiers));
                cx.notify();
            }))
            .child(place(0.0, 0.0, self.layout.width, self.layout.height).child(img(bg).size_full().object_fit(ObjectFit::Fill)));
        for i in 0..self.layout.parts.len() {
            let p = self.layout.parts[i].clone();
            let sprite = self.sprite(i, dev);
            root = root.child(place(p.x, p.y, p.w, p.h).child(img(sprite).size_full().object_fit(ObjectFit::Fill)));
        }
        root
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(config::WINDOW.0), px(config::WINDOW.1)), cx);
        cx.open_window(
            WindowOptions { window_bounds: Some(WindowBounds::Windowed(bounds)), ..Default::default() },
            |window, cx| {
                let view = cx.new(Device::new);
                view.read(cx).focus.focus(window);
                view
            },
        )
        .unwrap();
        cx.activate(true);
    });
}
