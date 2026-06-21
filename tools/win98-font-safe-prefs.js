// Conservative text rendering defaults for Windows 98/KernelEx testing.
// Modern downloadable fonts can confuse the old GDI font path and scramble text.
pref("gfx.downloadable_fonts.enabled", false);
pref("gfx.downloadable_fonts.woff2.enabled", false);
pref("gfx.font_rendering.graphite.enabled", false);
pref("gfx.font_rendering.opentype_svg.enabled", false);
