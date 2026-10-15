/*
 * Small vector glyphs drawn directly into a view (they scale with the
 * rectangle they are given, so they follow the font size).
 */
#ifndef ICONS_H
#define ICONS_H


#include <GraphicsDefs.h>
#include <Rect.h>


class BView;


enum glyph {
	GLYPH_PLAY,
	GLYPH_PAUSE,
	GLYPH_PREVIOUS,
	GLYPH_NEXT,
	GLYPH_SPEAKER,
	GLYPH_SPEAKER_LOUD,
	GLYPH_SHUFFLE,
	GLYPH_REPEAT,
	GLYPH_REPEAT_ONE,
	GLYPH_PLUS,
	GLYPH_MAGNIFIER,
	GLYPH_EYE,
	GLYPH_CLEAR,
	GLYPH_WARNING,
	GLYPH_EQUALIZER
};


void draw_glyph(BView* view, BRect rect, glyph which, rgb_color color);

// Source list icons
void draw_library_icon(BView* view, BRect rect);
void draw_playlist_icon(BView* view, BRect rect);
void draw_smart_playlist_icon(BView* view, BRect rect);

// Mixes two colors; amount 0 gives a, 255 gives b.
rgb_color mix(rgb_color a, rgb_color b, uint8 amount);


#endif	// ICONS_H
