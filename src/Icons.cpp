/*
 * Small vector glyphs.
 */
#include "Icons.h"

#include <GradientLinear.h>
#include <Shape.h>
#include <View.h>

#include <math.h>

#include <algorithm>


rgb_color
mix(rgb_color a, rgb_color b, uint8 amount)
{
	rgb_color result;
	result.red = (uint8)((a.red * (255 - amount) + b.red * amount) / 255);
	result.green = (uint8)((a.green * (255 - amount) + b.green * amount)
		/ 255);
	result.blue = (uint8)((a.blue * (255 - amount) + b.blue * amount) / 255);
	result.alpha = (uint8)((a.alpha * (255 - amount) + b.alpha * amount)
		/ 255);
	return result;
}


namespace {


// app_server puts shape coordinates on pixel edges, but rects and ellipses on
// pixel indices (x is the middle of pixel x). Moving the pen to kPixelCenter
// shifts shapes onto the index grid, and box() rounds the rect instead of
// letting app_server truncate it: the parts of a glyph line up with each
// other and stay centered in the rect the caller computed.
const BPoint kPixelCenter(0.5f, 0.5f);


// A square of the rect's smaller side, centered in it.
BRect
square(BRect rect)
{
	float size = std::min(rect.Width(), rect.Height());
	BPoint center((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
	return BRect(center.x - size / 2, center.y - size / 2,
		center.x + size / 2, center.y + size / 2);
}


BPoint
at(BRect r, float x, float y)
{
	return BPoint(r.left + x * r.Width(), r.top + y * r.Height());
}


// The pixel edge nearest to a shape coordinate (with the pen at kPixelCenter
// edges are at n - 0.5): straight sides on whole pixels look sharp. Snap the
// side above a center and mirror it (2 * center - top) for the side below to
// keep the shape symmetric.
float
snap(float coordinate)
{
	return roundf(coordinate + 0.5f) - 0.5f;
}


void
triangle(BView* view, BPoint a, BPoint b, BPoint c)
{
	BShape shape;
	shape.MoveTo(a);
	shape.LineTo(b);
	shape.LineTo(c);
	shape.Close();
	view->FillShape(&shape);
}


void
box(BView* view, BRect rect)
{
	view->FillRect(BRect(roundf(rect.left), roundf(rect.top),
		roundf(rect.right), roundf(rect.bottom)));
}


// StrokeLine() would align to its own grid and move the pen.
void
line(BView* view, BPoint a, BPoint b)
{
	BShape shape;
	shape.MoveTo(a);
	shape.LineTo(b);
	view->StrokeShape(&shape);
}


// A circular arc of up to about 120 degrees as one Bezier curve; angles in
// degrees, counterclockwise from three o'clock as in BView::StrokeArc().
void
add_arc(BShape& shape, BPoint center, float radius, float start, float span,
	bool move)
{
	float a0 = start * M_PI / 180;
	float a1 = (start + span) * M_PI / 180;
	float k = 4.0f / 3.0f * tanf((a1 - a0) / 4) * radius;
	BPoint p0(center.x + radius * cosf(a0), center.y - radius * sinf(a0));
	BPoint p1(center.x + radius * cosf(a1), center.y - radius * sinf(a1));
	if (move)
		shape.MoveTo(p0);
	shape.BezierTo(BPoint(p0.x - k * sinf(a0), p0.y - k * cosf(a0)),
		BPoint(p1.x + k * sinf(a1), p1.y + k * cosf(a1)), p1);
}


void
arc(BView* view, BPoint center, float radius, float start, float span)
{
	BShape shape;
	add_arc(shape, center, radius, start, span, true);
	view->StrokeShape(&shape);
}


void
circle(BView* view, BPoint center, float radius, bool fill)
{
	BShape shape;
	for (int i = 0; i < 4; i++)
		add_arc(shape, center, radius, i * 90, 90, i == 0);
	shape.Close();
	if (fill)
		view->FillShape(&shape);
	else
		view->StrokeShape(&shape);
}


// The middle of a line of a whole-pixel pen on the pixel grid, a pixel's
// middle for odd pens and a pixel edge for even ones: its sides are sharp.
float
on_grid(float coordinate, float pen)
{
	return fmodf(pen, 2) != 0 ? roundf(coordinate) : snap(coordinate);
}


// An arrowhead on a horizontal line put on the grid with on_grid(),
// pointing left or right; its base and corners fall on pixel edges.
void
arrow(BView* view, float tip, float base, float row, float pen, float length)
{
	float half = fmodf(pen, 2) != 0 ? floorf(length * 0.6f) + 0.5f
		: roundf(length * 0.6f);
	triangle(view, BPoint(tip, row), BPoint(base, row - half),
		BPoint(base, row + half));
}


BPoint
half_turn(BPoint point, BPoint center)
{
	return BPoint(2 * center.x - point.x, 2 * center.y - point.y);
}


void
draw_note(BView* view, BRect r, rgb_color color)
{
	view->SetHighColor(color);
	float s = r.Width();
	BPoint head1 = at(r, 0.32f, 0.74f);
	BPoint head2 = at(r, 0.72f, 0.64f);
	float rx = s * 0.15f;
	float ry = s * 0.11f;
	view->FillEllipse(head1, rx, ry);
	view->FillEllipse(head2, rx, ry);

	float stem = std::max(1.0f, s * 0.08f);
	float x1 = head1.x + rx - stem;
	float x2 = head2.x + rx - stem;
	float top1 = r.top + s * 0.24f;
	float top2 = r.top + s * 0.14f;
	box(view, BRect(x1, top1, x1 + stem, head1.y));
	box(view, BRect(x2, top2, x2 + stem, head2.y));

	BShape beam;
	float thickness = s * 0.14f;
	beam.MoveTo(BPoint(x1, top1));
	beam.LineTo(BPoint(x2 + stem, top2));
	beam.LineTo(BPoint(x2 + stem, top2 + thickness));
	beam.LineTo(BPoint(x1, top1 + thickness));
	beam.Close();
	view->FillShape(&beam);
}


void
rounded_tile(BView* view, BRect r, rgb_color top, rgb_color bottom)
{
	BGradientLinear gradient(r.LeftTop(), r.LeftBottom());
	gradient.AddColor(top, 0);
	gradient.AddColor(bottom, 255);
	float radius = r.Width() * 0.2f;
	view->FillRoundRect(r, radius, radius, gradient);
	view->SetHighColor(mix(bottom, make_color(0, 0, 0, 255), 60));
	view->SetPenSize(1);
	view->StrokeRoundRect(r, radius, radius);
}


}	// namespace


// Shapes are drawn relative to the pen location (BColumnListView, for one,
// moves the pen before drawing a field): every function here starts with
// PushState() and MovePenTo(kPixelCenter).


void
draw_glyph(BView* view, BRect rect, glyph which, rgb_color color)
{
	view->PushState();
	view->MovePenTo(kPixelCenter);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(color);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	BRect r = square(rect);
	float s = r.Width();

	switch (which) {
		case GLYPH_PLAY:
		{
			float middle = r.top + s / 2;
			float left = snap(r.left + s * 0.22f);
			float top = snap(middle - s * 0.4f);
			triangle(view, BPoint(left, top), BPoint(left, 2 * middle - top),
				BPoint(r.left + s * 0.92f, middle));
			break;
		}

		case GLYPH_PAUSE:
		{
			// pixel columns mirrored around the middle, at least one apart
			float middle = r.left + s / 2;
			float outer = roundf(middle - s * 0.32f);
			float inner = std::min(roundf(middle - s * 0.08f),
				floorf(middle - 1));
			float top = roundf(r.top + s * 0.12f);
			float bottom = 2 * (r.top + s / 2) - top;
			view->FillRect(BRect(outer, top, inner, bottom));
			view->FillRect(BRect(2 * middle - inner, top, 2 * middle - outer,
				bottom));
			break;
		}

		case GLYPH_PREVIOUS:
			box(view, BRect(at(r, 0.04f, 0.2f), at(r, 0.14f, 0.8f)));
			triangle(view, at(r, 0.14f, 0.5f), at(r, 0.56f, 0.2f),
				at(r, 0.56f, 0.8f));
			triangle(view, at(r, 0.54f, 0.5f), at(r, 0.96f, 0.2f),
				at(r, 0.96f, 0.8f));
			break;

		case GLYPH_NEXT:
			triangle(view, at(r, 0.04f, 0.2f), at(r, 0.04f, 0.8f),
				at(r, 0.46f, 0.5f));
			triangle(view, at(r, 0.44f, 0.2f), at(r, 0.44f, 0.8f),
				at(r, 0.86f, 0.5f));
			box(view, BRect(at(r, 0.86f, 0.2f), at(r, 0.96f, 0.8f)));
			break;

		case GLYPH_SPEAKER:
		case GLYPH_SPEAKER_LOUD:
		{
			float middle = r.top + s / 2;
			float back = snap(r.left + s * 0.08f);
			float neck = r.left + s * 0.28f;
			float front = snap(r.left + s * 0.52f);
			float boxTop = snap(middle - s * 0.14f);
			float coneTop = snap(middle - s * 0.36f);
			BShape body;
			body.MoveTo(BPoint(back, boxTop));
			body.LineTo(BPoint(neck, boxTop));
			body.LineTo(BPoint(front, coneTop));
			body.LineTo(BPoint(front, 2 * middle - coneTop));
			body.LineTo(BPoint(neck, 2 * middle - boxTop));
			body.LineTo(BPoint(back, 2 * middle - boxTop));
			body.Close();
			view->FillShape(&body);
			if (which == GLYPH_SPEAKER_LOUD) {
				// shapes rather than StrokeArc(), which centers arcs on its
				// own grid and put the waves below the cone
				view->SetPenSize(std::max(1.0f, s * 0.08f));
				BPoint center = at(r, 0.5f, 0.5f);
				arc(view, center, s * 0.18f, -45, 90);
				arc(view, center, s * 0.34f, -50, 100);
			}
			break;
		}

		case GLYPH_SHUFFLE:
		{
			// two lines swapping rows between straight ends, mirrored top
			// to bottom. A whole-pixel pen on the grid keeps the straight
			// parts sharp (a fractional one smeared them over two rows);
			// the loose ends and the arrowhead bases are on pixel edges.
			float pen = std::max(1.0f, roundf(s * 0.085f));
			float middle = r.top + s / 2;
			float upper = on_grid(r.top + s * 0.25f, pen);
			float lower = 2 * middle - upper;
			float start = snap(r.left + s * 0.04f);
			float bend = r.left + s * 0.2f;
			float tip = snap(r.right);
			float length = std::max(3.0f, roundf(s * 0.3f));
			float base = tip - length;
			float control = (bend + base) / 2;
			view->SetPenSize(pen);
			view->SetLineMode(B_BUTT_CAP, B_ROUND_JOIN);
			for (int i = 0; i < 2; i++) {
				float from = i == 0 ? upper : lower;
				float to = i == 0 ? lower : upper;
				BShape line;
				line.MoveTo(BPoint(start, from));
				line.LineTo(BPoint(bend, from));
				line.BezierTo(BPoint(control, from), BPoint(control, to),
					BPoint(base, to));
				view->StrokeShape(&line);
				arrow(view, tip, base, to, pen, length);
			}
			break;
		}

		case GLYPH_REPEAT:
		case GLYPH_REPEAT_ONE:
		{
			// two halves of a loop, half a turn apart, each ending in an
			// arrowhead; a whole-pixel pen, rows, columns and corner radius
			// keep the straight parts sharp, as for the shuffle glyph
			float pen = std::max(1.0f, roundf(s * 0.085f));
			BPoint center(r.left + s / 2, r.top + s / 2);
			float top = on_grid(r.top + s * 0.2f, pen);
			float left = on_grid(r.left + s * 0.12f, pen);
			float radius = std::max(1.0f, roundf(s * 0.2f));
			float k = radius * 0.5523f;
				// the control point distance for a quarter circle
			float open = snap(r.top + s * 0.62f);
			float tip = snap(r.right - s * 0.04f);
			float length = std::max(3.0f, roundf(s * 0.3f));
			float base = tip - length;
			view->SetPenSize(pen);
			view->SetLineMode(B_BUTT_CAP, B_ROUND_JOIN);
			for (int i = 0; i < 2; i++) {
				BPoint points[6] = {
					BPoint(left, open), BPoint(left, top + radius),
					BPoint(left, top + radius - k),
					BPoint(left + radius - k, top), BPoint(left + radius, top),
					BPoint(base, top)
				};
				if (i == 1) {
					for (int j = 0; j < 6; j++)
						points[j] = half_turn(points[j], center);
				}
				BShape half;
				half.MoveTo(points[0]);
				half.LineTo(points[1]);
				half.BezierTo(points[2], points[3], points[4]);
				half.LineTo(points[5]);
				view->StrokeShape(&half);
			}
			arrow(view, tip, base, top, pen, length);
			arrow(view, 2 * center.x - tip, 2 * center.x - base,
				2 * center.y - top, pen, length);
			if (which == GLYPH_REPEAT_ONE) {
				// a "1" in the loop, a pixel clear of its lines: rows are
				// pixel indices from the line's lower edge (top + pen / 2)
				float stem = std::max(1.0f, roundf(s * 0.08f));
				float column = roundf(center.x - (stem - 1) / 2);
				float first = top + pen / 2 + 1.5f;
				float last = 2 * center.y - first;
				view->FillRect(BRect(column, first, column + stem - 1, last));
				view->FillRect(BRect(column - stem, first, column - 1,
					first + stem - 1));
			}
			break;
		}

		case GLYPH_PLUS:
		{
			// two bars of the same whole-pixel thickness and length crossing
			// in their middles: rounded each on its own, they came out 2 and
			// 3 pixels thick when the middle was a pixel edge one way and a
			// pixel center the other. The cross moves by up to half a pixel
			// to fit the pixel grid both ways.
			float thickness = std::max(1.0f, roundf(s * 0.15f));
			float arm = std::max(1.0f, roundf((s * 0.72f - thickness) / 2));
			float left = roundf(r.left + s / 2 - (thickness - 1) / 2);
			float top = roundf(r.top + s / 2 - (thickness - 1) / 2);
			float right = left + thickness - 1;
			float bottom = top + thickness - 1;
			view->FillRect(BRect(left - arm, top, right + arm, bottom));
			view->FillRect(BRect(left, top - arm, right, bottom + arm));
			break;
		}

		case GLYPH_MAGNIFIER:
		{
			float pen = std::max(1.2f, s * 0.11f);
			view->SetPenSize(pen);
			BPoint center = at(r, 0.42f, 0.42f);
			view->StrokeEllipse(center, s * 0.27f, s * 0.27f);
			view->SetPenSize(pen * 1.4f);
			line(view, at(r, 0.64f, 0.64f), at(r, 0.88f, 0.88f));
			break;
		}

		case GLYPH_EYE:
		{
			// lids with a heavier lash line on top around a lightly toned
			// eyeball, the iris, the pupil and a catchlight
			BPoint center = at(r, 0.5f, 0.5f);
			BPoint inner = at(r, 0.02f, 0.5f);
			BPoint outer = at(r, 0.98f, 0.5f);
			BShape lashLine;
			lashLine.MoveTo(inner);
			lashLine.BezierTo(at(r, 0.27f, 0.09f), at(r, 0.73f, 0.09f), outer);
			BShape lids(lashLine);
			lids.BezierTo(at(r, 0.73f, 0.91f), at(r, 0.27f, 0.91f), inner);
			lids.Close();

			rgb_color tone = color;
			tone.alpha = color.alpha * 30 / 255;
			view->SetHighColor(tone);
			view->FillShape(&lids);
			float pen = std::max(1.0f, s * 0.075f);
			view->SetHighColor(color);
			view->SetPenSize(pen);
			view->StrokeShape(&lids);
			view->SetPenSize(pen * 1.8f);
			view->StrokeShape(&lashLine);

			tone.alpha = color.alpha * 120 / 255;
			view->SetHighColor(tone);
			circle(view, center, s * 0.2f, true);
			view->SetHighColor(color);
			circle(view, center, s * 0.1f, true);

			view->SetHighColor(color.IsLight() ? make_color(0, 0, 0, 150)
				: make_color(255, 255, 255, 230));
			circle(view, BPoint(center.x - s * 0.07f, center.y - s * 0.07f),
				std::max(0.8f, s * 0.05f), true);
			break;
		}

		case GLYPH_CLEAR:
		{
			view->FillEllipse(at(r, 0.5f, 0.5f), s * 0.5f, s * 0.5f);
			view->SetHighColor(255, 255, 255);
			view->SetPenSize(std::max(1.2f, s * 0.13f));
			line(view, at(r, 0.32f, 0.32f), at(r, 0.68f, 0.68f));
			line(view, at(r, 0.68f, 0.32f), at(r, 0.32f, 0.68f));
			break;
		}

		case GLYPH_EQUALIZER:
		{
			// three sliders at different heights, on whole pixel columns:
			// stems and knobs of odd widths centered on them, and knobs at
			// least a pixel narrower than the spacing so they stay apart
			float middle = floorf(r.left + s / 2);
			float spacing = std::max(4.0f, roundf(s * 0.3f));
			float stem = 1 + 2 * floorf(s * 0.04f);
			float knob = 1 + 2 * floorf(s * 0.13f);
			float widest = spacing - 1 - fmodf(spacing, 2);
			knob = std::max(stem, std::min(knob, widest));
			float knobHeight = std::max(2.0f, roundf(s * 0.16f));
			float top = roundf(r.top + s * 0.06f);
			float bottom = 2 * (r.top + s / 2) - top;
			const float ys[3] = { 0.66f, 0.32f, 0.54f };
			for (int i = 0; i < 3; i++) {
				float x = middle + (i - 1) * spacing;
				view->FillRect(BRect(x - (stem - 1) / 2, top,
					x + (stem - 1) / 2, bottom));
				float knobTop = roundf(r.top + s * ys[i] - knobHeight / 2);
				view->FillRect(BRect(x - (knob - 1) / 2, knobTop,
					x + (knob - 1) / 2, knobTop + knobHeight - 1));
			}
			break;
		}

		case GLYPH_WARNING:
		{
			view->FillEllipse(at(r, 0.5f, 0.5f), s * 0.45f, s * 0.45f);
			view->SetHighColor(255, 255, 255);
			float w = std::max(1.0f, s * 0.12f);
			float barBottom = roundf(r.top + s * 0.6f);
			float dotTop = std::max(roundf(r.top + s * 0.68f), barBottom + 2);
			box(view, BRect(r.left + s / 2 - w / 2, r.top + s * 0.22f,
				r.left + s / 2 + w / 2, barBottom));
			box(view, BRect(r.left + s / 2 - w / 2, dotTop,
				r.left + s / 2 + w / 2, dotTop + w));
			break;
		}
	}

	view->PopState();
}


void
draw_library_icon(BView* view, BRect rect)
{
	view->PushState();
	view->MovePenTo(kPixelCenter);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	BRect r = square(rect);
	rounded_tile(view, r, make_color(110, 170, 240, 255),
		make_color(30, 90, 190, 255));
	draw_note(view, r.InsetByCopy(r.Width() * 0.12f, r.Width() * 0.12f),
		make_color(255, 255, 255, 255));
	view->PopState();
}


void
draw_playlist_icon(BView* view, BRect rect)
{
	view->PushState();
	view->MovePenTo(kPixelCenter);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	BRect r = square(rect);
	// the library icon's note, in the playlist colors
	rounded_tile(view, r, make_color(250, 252, 255, 255),
		make_color(190, 210, 235, 255));
	draw_note(view, r.InsetByCopy(r.Width() * 0.12f, r.Width() * 0.12f),
		make_color(40, 100, 200, 255));
	view->PopState();
}


void
draw_smart_playlist_icon(BView* view, BRect rect)
{
	view->PushState();
	view->MovePenTo(kPixelCenter);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	BRect r = square(rect);
	rounded_tile(view, r, make_color(190, 140, 230, 255),
		make_color(110, 50, 170, 255));

	// a gear
	BPoint center = at(r, 0.5f, 0.5f);
	float outer = r.Width() * 0.36f;
	float inner = r.Width() * 0.26f;
	BShape gear;
	const int kSteps = 32;
	for (int i = 0; i < kSteps; i++) {
		float angle = i * 2 * M_PI / kSteps + M_PI / kSteps;
		float radius = (i % 4) < 2 ? outer : inner;
		BPoint point(center.x + cosf(angle) * radius,
			center.y + sinf(angle) * radius);
		if (i == 0)
			gear.MoveTo(point);
		else
			gear.LineTo(point);
	}
	gear.Close();
	view->SetHighColor(255, 255, 255);
	view->FillShape(&gear);
	view->SetHighColor(140, 80, 195);
	view->FillEllipse(center, r.Width() * 0.1f, r.Width() * 0.1f);
	view->PopState();
}
