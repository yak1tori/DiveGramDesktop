// This file is part of DiveGram,
// an unofficial Telegram Desktop application.
//
// For license and copyright information please follow this link:
// https://github.com/DiveGram/DiveGramDesktop/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <QtCore/QSize>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtGui/QRadialGradient>

#include <cmath>
#include <utility>
#include <vector>

namespace Dialogs::LiquidGlass {

namespace {

struct PillKey {
	QSize size;
	int radius = 0;
	int scale = 100;
};

inline bool operator==(const PillKey &a, const PillKey &b) {
	return a.size == b.size && a.radius == b.radius && a.scale == b.scale;
}

inline QColor GlassColor(int alpha) {
	return QColor(255, 255, 255, alpha);
}

inline QColor GlassDark(int alpha) {
	return QColor(0, 0, 0, alpha);
}

inline QPainterPath PillPath(const QRectF &rect, float64 radius) {
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	return path;
}

inline float64 Clamp01(float64 v) {
	return (v < 0.) ? 0. : ((v > 1.) ? 1. : v);
}

inline void PaintPillFill(
		QPainter &p,
		const QRectF &rect,
		float64 radius) {
	auto fill = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	fill.setColorAt(0., GlassColor(18));
	fill.setColorAt(0.45, GlassColor(9));
	fill.setColorAt(1., GlassColor(4));

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(PillPath(rect, radius));
	p.fillRect(rect, fill);
	p.restore();
}

// Directional light field (litfield in the liquidDX11 glass shader):
// the surface brightens towards the light and gradually dims away from it.
// Painted as a diagonal white gradient oriented along the light direction.
inline void PaintLiquidGlassLight(
		QPainter &p,
		const QRectF &rect,
		float64 radius,
		const QPointF &light) {
	if (light.isNull()) {
		return;
	}
	const auto cx = rect.center();
	const auto w = rect.width();
	const auto h = rect.height();
	const auto lx = light.x();
	const auto ly = light.y();
	const auto len = qSqrt(lx * lx + ly * ly);
	if (len < 1e-6) {
		return;
	}
	const auto ux = lx / len;
	const auto uy = ly / len;

	// Corners projected on the light vector.
	const auto dot00 = ux * (-0.5 * w) + uy * (-0.5 * h);
	const auto dot11 = ux * ( 0.5 * w) + uy * ( 0.5 * h);
	const auto t0 = std::min(dot00, dot11);
	const auto t1 = std::max(dot00, dot11);

	const auto from = cx + QPointF(ux * t1, uy * t1);
	const auto to = cx - QPointF(ux * (t1 - t0) * 1.2, uy * (t1 - t0) * 1.2);

	auto lit = QLinearGradient(from, to);
	const auto brightAlpha = 9;
	lit.setColorAt(0., GlassColor(brightAlpha));
	lit.setColorAt(0.35, GlassColor(brightAlpha * 0.25));
	lit.setColorAt(1., GlassColor(0));

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(PillPath(rect, radius));
	p.fillRect(rect, lit);
	p.restore();
}

inline void PaintLiquidGlassSpecular(
		QPainter &p,
		const QRectF &rect,
		float64 radius,
		const QPointF &light) {
	if (light.isNull()) {
		return;
	}
	const auto w = rect.width();
	const auto h = rect.height();
	const auto ux = light.x();
	const auto uy = light.y();
	const auto len = qSqrt(ux * ux + uy * uy);
	if (len < 1e-6) {
		return;
	}
	const auto nx = ux / len;
	const auto ny = uy / len;

	// Specular lobe pushed along the light direction, slightly above center.
	const auto center = QPointF(
		rect.x() + w * (0.5 + nx * 0.22),
		rect.y() + h * (0.5 + ny * 0.22));
	const auto rx = w * 0.42;
	const auto ry = h * 0.75;

	auto glow = QRadialGradient(center, qMax(rx, ry));
	glow.setColorAt(0., GlassColor(11));
	glow.setColorAt(0.45, GlassColor(4));
	glow.setColorAt(1., GlassColor(0));

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(PillPath(rect, radius));
	p.fillRect(rect, glow);
	p.restore();
}

inline void PaintPillSheen(
		QPainter &p,
		const QRectF &rect,
		float64 radius) {
	const auto sheenHeight = qMin(rect.height() * 0.5, 14.);
	const auto sheenRect = QRectF(
		rect.x() + radius * 0.4,
		rect.y(),
		rect.width() - radius * 0.8,
		sheenHeight);
	if (sheenRect.isEmpty()) {
		return;
	}
	auto sheen = QLinearGradient(
		sheenRect.topLeft(),
		sheenRect.bottomLeft());
	sheen.setColorAt(0., GlassColor(10));
	sheen.setColorAt(1., GlassColor(0));

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(PillPath(rect, radius));
	p.fillRect(sheenRect, sheen);
	p.restore();
}

inline void PaintPillRim(
		QPainter &p,
		const QRectF &rect,
		float64 radius) {
	const auto inset = 1.;
	const auto inner = QRectF(rect).adjusted(inset, inset, -inset, -inset);

	auto halo = QLinearGradient(inner.topLeft(), inner.bottomLeft());
	halo.setColorAt(0., GlassColor(9));
	halo.setColorAt(0.45, GlassColor(2));
	halo.setColorAt(1., GlassColor(1));

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipping(false);
	p.setPen(QPen(QBrush(halo), 1.8));
	p.setBrush(Qt::NoBrush);
	p.drawPath(PillPath(inner, radius));
	p.restore();

	auto softInner = QLinearGradient(inner.topLeft(), inner.bottomLeft());
	softInner.setColorAt(0.5, GlassDark(0));
	softInner.setColorAt(1., GlassDark(9));

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipping(false);
	p.setPen(QPen(QBrush(softInner), 1.8));
	p.setBrush(Qt::NoBrush);
	p.drawPath(PillPath(inner, radius));
	p.restore();
}

inline void PaintPillShadow(
		QPainter &p,
		const QRectF &rect,
		float64 radius) {
	const auto plate = PillPath(rect, radius);
	const auto below = [&](float top, float bottom, float inflate, float rad) {
		return PillPath(
			QRectF(rect).adjusted(-inflate, top, inflate, bottom),
			rad).subtracted(plate);
	};

	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipping(false);
	p.fillPath(below(2.2, 2.6, 0.6, radius), GlassDark(6));
	p.fillPath(below(2.0, 2.0, 0.9, radius), GlassDark(12));
	p.fillPath(below(1.8, 1.4, 1.2, radius), GlassDark(18));
	p.restore();
}

inline QPixmap PreparePill(const QSize &size, int radius, float scale) {
	static std::vector<std::pair<PillKey, QPixmap>> cache;
	constexpr auto kCacheLimit = 8;
	constexpr auto kPad = 4; // logical px of room for the drop shadow

	const auto key = PillKey{
		size,
		radius,
		int(std::lround(scale * 100.f)),
	};

	for (auto i = cache.begin(); i != cache.end(); ++i) {
		if (i->first == key) {
			auto result = std::move(i->second);
			cache.erase(i);
			cache.emplace(cache.begin(), key, std::move(result));
			return cache.front().second;
		}
	}

	const auto total = QSize(
		size.width() + 2 * kPad,
		size.height() + 2 * kPad);
	const auto pixmapSize = QSize(
		qMax(1, qRound(total.width() * scale)),
		qMax(1, qRound(total.height() * scale)));
	auto pixmap = QPixmap(pixmapSize);
	pixmap.setDevicePixelRatio(scale);
	pixmap.fill(Qt::transparent);

	{
		QPainter p(&pixmap);
		p.scale(scale, scale);
		const auto rect = QRectF(
			QPointF(kPad, kPad),
			QSizeF(size));
		const auto radiusf = float64(radius);
		const auto light = QPointF(-0.42, -0.9);
		PaintPillShadow(p, rect, radiusf);
		PaintPillFill(p, rect, radiusf);
		PaintLiquidGlassLight(p, rect, radiusf, light);
		PaintLiquidGlassSpecular(p, rect, radiusf, light);
		PaintPillSheen(p, rect, radiusf);
		PaintPillRim(p, rect, radiusf);
	}

	while (cache.size() >= kCacheLimit) {
		cache.pop_back();
	}
	cache.emplace(cache.begin(), key, pixmap);
	return cache.front().second;
}

} // namespace

inline void PaintLiquidGlassTint(
		QPainter &p,
		const QRect &rect,
		int radius,
		const QColor &tint) {
	if (rect.isEmpty()) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(tint);
	p.drawPath(PillPath(QRectF(rect), float64(radius)));
	p.restore();
}

inline void PaintLiquidGlassPill(
		QPainter &p,
		const QRect &rect,
		int radius) {
	if (rect.isEmpty()) {
		return;
	}
	const auto scale = float(p.device()->devicePixelRatioF());
	const auto pad = 4.f;
	const auto source = QRectF(0., 0., rect.width() + 2 * pad, rect.height() + 2 * pad);
	p.drawPixmap(
		QRectF(
			rect.x() - pad,
			rect.y() - pad,
			rect.width() + 2 * pad,
			rect.height() + 2 * pad),
		PreparePill(rect.size(), radius, scale),
		source);
}

inline void PaintLiquidGlassEdge(
		QPainter &p,
		const QRect &rect,
		int radius) {
	if (rect.isEmpty()) {
		return;
	}
	const auto rectf = QRectF(rect);
	const auto radiusf = float64(radius);
	PaintPillSheen(p, rectf, radiusf);
	PaintPillRim(p, rectf, radiusf);
}

} // namespace Dialogs::LiquidGlass