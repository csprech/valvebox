//! The C bridge (bridge/PanelBridge.h), wrapped. Everything runs on the main thread.

use std::ffi::{CStr, CString, c_char, c_float, c_int};

#[repr(C)]
pub struct JpPanel {
    _private: [u8; 0],
}

unsafe extern "C" {
    fn jp_create() -> *mut JpPanel;
    fn jp_destroy(p: *mut JpPanel);
    fn jp_layout_json(p: *mut JpPanel) -> *const c_char;
    fn jp_render_background(p: *mut JpPanel, scale: c_float, bgra: *mut u8, w: c_int, h: c_int) -> c_int;
    fn jp_signature(p: *mut JpPanel, index: c_int) -> u64;
    fn jp_render_part(p: *mut JpPanel, index: c_int, scale: c_float, bgra: *mut u8, w: c_int, h: c_int) -> c_int;
    fn jp_mouse(p: *mut JpPanel, kind: c_int, x: c_float, y: c_float, mods: c_int, clicks: c_int);
    fn jp_wheel(p: *mut JpPanel, x: c_float, y: c_float, dx: c_float, dy: c_float, mods: c_int);
    fn jp_key(p: *mut JpPanel, key: *const c_char, chars: *const c_char, down: c_int, mods: c_int) -> c_int;
    fn jp_modifiers(p: *mut JpPanel, mods: c_int);
    fn jp_output_level(p: *mut JpPanel) -> c_float;
    fn jp_test_sound(p: *mut JpPanel, start: c_int);
}

pub struct Panel(*mut JpPanel);

pub enum Mouse {
    Down,
    Drag,
    Up,
}

impl Panel {
    pub fn new() -> Self {
        Self(unsafe { jp_create() })
    }
    pub fn layout_json(&self) -> String {
        unsafe { CStr::from_ptr(jp_layout_json(self.0)) }.to_string_lossy().into_owned()
    }
    pub fn render_background(&self, scale: f32, w: usize, h: usize) -> Vec<u8> {
        let mut px = vec![0u8; w * h * 4];
        unsafe { jp_render_background(self.0, scale, px.as_mut_ptr(), w as c_int, h as c_int) };
        px
    }
    pub fn signature(&self, index: usize) -> u64 {
        unsafe { jp_signature(self.0, index as c_int) }
    }
    pub fn render_part(&self, index: usize, scale: f32, w: usize, h: usize) -> Vec<u8> {
        let mut px = vec![0u8; w * h * 4];
        unsafe { jp_render_part(self.0, index as c_int, scale, px.as_mut_ptr(), w as c_int, h as c_int) };
        px
    }
    pub fn mouse(&self, kind: Mouse, x: f32, y: f32, mods: i32, clicks: i32) {
        let k = match kind {
            Mouse::Down => 0,
            Mouse::Drag => 1,
            Mouse::Up => 2,
        };
        unsafe { jp_mouse(self.0, k, x, y, mods, clicks) }
    }
    pub fn wheel(&self, x: f32, y: f32, dx: f32, dy: f32, mods: i32) {
        unsafe { jp_wheel(self.0, x, y, dx, dy, mods) }
    }
    pub fn key(&self, key: &str, chars: &str, down: bool, mods: i32) -> bool {
        let k = CString::new(key).unwrap_or_default();
        let c = CString::new(chars).unwrap_or_default();
        unsafe { jp_key(self.0, k.as_ptr(), c.as_ptr(), down as c_int, mods) != 0 }
    }
    pub fn modifiers(&self, mods: i32) {
        unsafe { jp_modifiers(self.0, mods) }
    }
    pub fn output_level(&self) -> f32 {
        unsafe { jp_output_level(self.0) }
    }
    pub fn test_sound(&self, start: bool) {
        unsafe { jp_test_sound(self.0, start as c_int) }
    }
}

impl Drop for Panel {
    fn drop(&mut self) {
        unsafe { jp_destroy(self.0) }
    }
}
