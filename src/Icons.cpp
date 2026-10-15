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
arrow_head(BView* view, BPoint tip, float angle, float size)
{
	BPoint back(tip.x - cosf(angle) * size, tip.y - sinf(angle) * size);
	BPoint side(-sinf(angle) * size * 0.6f, cosf(angle) * size * 0.6f);
	triangle(view, tip, back + side, back - side);
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
	view->FillRect(BRect(x1, top1, x1 + stem, head1.y));
	view->FillRect(BRect(x2, top2, x2 + stem, head2.y));

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
// PushState() and MovePenTo(B_ORIGIN).


void
draw_glyph(BView* view, BRect rect, glyph which, rgb_color color)
{
	view->PushState();
	view->MovePenTo(B_ORIGIN);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(color);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	BRect r = square(rect);
	float s = r.Width();

	switch (which) {
		case GLYPH_PLAY:
			triangle(view, at(r, 0.22f, 0.1f), at(r, 0.22f, 0.9f),
				at(r, 0.92f, 0.5f));
			break;

		case GLYPH_PAUSE:
			view->FillRect(BRect(at(r, 0.18f, 0.12f), at(r, 0.42f, 0.88f)));
			view->FillRect(BRect(at(r, 0.58f, 0.12f), at(r, 0.82f, 0.88f)));
			break;

		case GLYPH_PREVIOUS:
			view->FillRect(BRect(at(r, 0.04f, 0.2f), at(r, 0.14f, 0.8f)));
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
			view->FillRect(BRect(at(r, 0.86f, 0.2f), at(r, 0.96f, 0.8f)));
			break;

		case GLYPH_SPEAKER:
		case GLYPH_SPEAKER_LOUD:
		{
			BShape body;
			body.MoveTo(at(r, 0.08f, 0.36f));
			body.LineTo(at(r, 0.28f, 0.36f));
			body.LineTo(at(r, 0.52f, 0.14f));
			body.LineTo(at(r, 0.52f, 0.86f));
			body.LineTo(at(r, 0.28f, 0.64f));
			body.LineTo(at(r, 0.08f, 0.64f));
			body.Close();
			view->FillShape(&body);
			if (which == GLYPH_SPEAKER_LOUD) {
				view->SetPenSize(std::max(1.0f, s * 0.08f));
				BPoint center = at(r, 0.5f, 0.5f);
				view->StrokeArc(center, s * 0.18f, s * 0.18f, -45, 90);
				view->StrokeArc(center, s * 0.34f, s * 0.34f, -50, 100);
			}
			break;
		}

		case GLYPH_SHUFFLE:
		{
			float pen = std::max(1.4f, s * 0.11f);
			view->SetPenSize(pen);
			BShape down;
			down.MoveTo(at(r, 0.04f, 0.27f));
			down.LineTo(at(r, 0.2f, 0.27f));
			BPoint control[3] = { at(r, 0.44f, 0.27f), at(r, 0.46f, 0.73f),
				at(r, 0.7f, 0.73f) };
			down.BezierTo(control);
			view->StrokeShape(&down);
			BShape up;
			up.MoveTo(at(r, 0.04f, 0.73f));
			up.LineTo(at(r, 0.2f, 0.73f));
			BPoint control2[3] = { at(r, 0.44f, 0.73f), at(r, 0.46f, 0.27f),
				at(r, 0.7f, 0.27f) };
			up.BezierTo(control2);
			view->StrokeShape(&up);
			view->SetPenSize(1);
			arrow_head(view, at(r, 1.0f, 0.73f), 0, s * 0.32f);
			arrow_head(view, at(r, 1.0f, 0.27f), 0, s * 0.32f);
			break;
		}

		case GLYPH_REPEAT:
		case GLYPH_REPEAT_ONE:
		{
			// two arrows chasing each other around a loop
			float pen = std::max(1.4f, s * 0.11f);
			view->SetPenSize(pen);
			BShape top;
			top.MoveTo(at(r, 0.12f, 0.6f));
			top.LineTo(at(r, 0.12f, 0.44f));
			BPoint c1[3] = { at(r, 0.12f, 0.28f), at(r, 0.18f, 0.24f),
				at(r, 0.32f, 0.24f) };
			top.BezierTo(c1);
			top.LineTo(at(r, 0.72f, 0.24f));
			view->StrokeShape(&top);
			BShape bottom;
			bottom.MoveTo(at(r, 0.88f, 0.4f));
			bottom.LineTo(at(r, 0.88f, 0.56f));
			BPoint c2[3] = { at(r, 0.88f, 0.72f), at(r, 0.82f, 0.76f),
				at(r, 0.68f, 0.76f) };
			bottom.BezierTo(c2);
			bottom.LineTo(at(r, 0.28f, 0.76f));
			view->StrokeShape(&bottom);
			view->SetPenSize(1);
			arrow_head(view, at(r, 0.96f, 0.24f), 0, s * 0.3f);
			arrow_head(view, at(r, 0.04f, 0.76f), M_PI, s * 0.3f);
			if (which == GLYPH_REPEAT_ONE) {
				// a "1" inside the loop
				float stroke = std::max(1.2f, s * 0.1f);
				BPoint center = at(r, 0.5f, 0.5f);
				float half = s * 0.17f;
				view->FillRect(BRect(center.x - stroke / 2, center.y - half,
					center.x + stroke / 2, center.y + half));
				view->FillRect(BRect(center.x - stroke * 1.6f, center.y - half,
					center.x, center.y - half + stroke));
			}
			break;
		}

		case GLYPH_PLUS:
		{
			float pen = std::max(1.5f, s * 0.14f);
			view->FillRect(BRect(r.left + s * 0.14f, r.top + s / 2 - pen / 2,
				r.right - s * 0.14f, r.top + s / 2 + pen / 2));
			view->FillRect(BRect(r.left + s / 2 - pen / 2, r.top + s * 0.14f,
				r.left + s / 2 + pen / 2, r.bottom - s * 0.14f));
			break;
		}

		case GLYPH_MAGNIFIER:
		{
			float pen = std::max(1.2f, s * 0.11f);
			view->SetPenSize(pen);
			BPoint center = at(r, 0.42f, 0.42f);
			view->StrokeEllipse(center, s * 0.27f, s * 0.27f);
			view->SetPenSize(pen * 1.4f);
			view->StrokeLine(at(r, 0.64f, 0.64f), at(r, 0.88f, 0.88f));
			break;
		}

		case GLYPH_EYE:
		{
			BShape outline;
			outline.MoveTo(at(r, 0.04f, 0.5f));
			BPoint top[3] = { at(r, 0.3f, 0.12f), at(r, 0.7f, 0.12f),
				at(r, 0.96f, 0.5f) };
			outline.BezierTo(top);
			BPoint bottom[3] = { at(r, 0.7f, 0.88f), at(r, 0.3f, 0.88f),
				at(r, 0.04f, 0.5f) };
			outline.BezierTo(bottom);
			outline.Close();
			view->SetPenSize(std::max(1.0f, s * 0.08f));
			view->StrokeShape(&outline);
			view->FillEllipse(at(r, 0.5f, 0.5f), s * 0.17f, s * 0.17f);
			break;
		}

		case GLYPH_CLEAR:
		{
			view->FillEllipse(at(r, 0.5f, 0.5f), s * 0.5f, s * 0.5f);
			view->SetHighColor(255, 255, 255);
			view->SetPenSize(std::max(1.2f, s * 0.13f));
			view->StrokeLine(at(r, 0.32f, 0.32f), at(r, 0.68f, 0.68f));
			view->StrokeLine(at(r, 0.68f, 0.32f), at(r, 0.32f, 0.68f));
			break;
		}

		case GLYPH_EQUALIZER:
		{
			// three sliders at different heights
			float pen = std::max(1.0f, s * 0.08f);
			float knobWidth = s * 0.26f;
			float knobHeight = std::max(2.0f, s * 0.16f);
			const float xs[3] = { 0.2f, 0.5f, 0.8f };
			const float ys[3] = { 0.66f, 0.32f, 0.54f };
			for (int i = 0; i < 3; i++) {
				BPoint top = at(r, xs[i], 0.06f);
				view->FillRect(BRect(top.x - pen / 2, top.y, top.x + pen / 2,
					r.top + s * 0.94f));
				BPoint knob = at(r, xs[i], ys[i]);
				view->FillRoundRect(BRect(knob.x - knobWidth / 2,
					knob.y - knobHeight / 2, knob.x + knobWidth / 2,
					knob.y + knobHeight / 2), 1, 1);
			}
			break;
		}

		case GLYPH_WARNING:
		{
			view->FillEllipse(at(r, 0.5f, 0.5f), s * 0.45f, s * 0.45f);
			view->SetHighColor(255, 255, 255);
			float w = std::max(1.0f, s * 0.12f);
			view->FillRect(BRect(r.left + s / 2 - w / 2, r.top + s * 0.22f,
				r.left + s / 2 + w / 2, r.top + s * 0.6f));
			view->FillRect(BRect(r.left + s / 2 - w / 2, r.top + s * 0.68f,
				r.left + s / 2 + w / 2, r.top + s * 0.68f + w));
			break;
		}
	}

	view->PopState();
}


void
draw_library_icon(BView* view, BRect rect)
{
	view->PushState();
	view->MovePenTo(B_ORIGIN);
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
	view->MovePenTo(B_ORIGIN);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	BRect r = square(rect);
	rounded_tile(view, r, make_color(250, 252, 255, 255),
		make_color(190, 210, 235, 255));
	draw_note(view, r.InsetByCopy(r.Width() * 0.14f, r.Width() * 0.14f),
		make_color(40, 100, 200, 255));
	view->PopState();
}


void
draw_smart_playlist_icon(BView* view, BRect rect)
{
	view->PushState();
	view->MovePenTo(B_ORIGIN);
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
