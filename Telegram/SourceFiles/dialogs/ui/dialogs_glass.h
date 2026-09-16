// This file is part of DiveGram,
// an unofficial Telegram Desktop application.
//
// For license and copyright information please follow this link:
// https://github.com/DiveGram/DiveGramDesktop/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"
#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"

class QPainter;

namespace Ui {
class ChatTheme;
} // namespace Ui

namespace Dialogs {

// Renders a blurred copy of the current chat wallpaper so translucent
// panels can sit on top of it with the iOS liquid glass look.
class GlassWallpaper final {
public:
	void paint(
			QPainter &p,
			const QRect &clip,
			QSize size,
			not_null<Ui::ChatTheme*> theme) {
		const auto &background = theme->background();
		if (background.prepared.isNull()
			&& background.gradientForFill.isNull()) {
			return;
		}
		auto key = background.key;
		if (!background.colors.empty()) {
			key += u"|%1"_q.arg(background.colors.front().rgba());
		}
		const auto snapped = QSize(
			((size.width() + 63) / 64) * 64,
			((size.height() + 63) / 64) * 64);
		if (_key != key || _size != snapped || _image.isNull()) {
			_key = key;
			_size = snapped;
			_image = generate(background, _size);
		}
		if (_image.isNull()) {
			return;
		}
		const auto dpr = _image.devicePixelRatio();
		const auto source = QRectF(
			QPointF(clip.topLeft()) * dpr,
			QSizeF(clip.size()) * dpr);
		p.drawImage(QRectF(clip), _image, source);
	}

private:
	[[nodiscard]] QImage generate(
			const Ui::ChatThemeBackground &background,
			QSize size) {
		auto source = background.prepared;
		if (source.isNull()) {
			source = background.gradientForFill;
		}
		const auto rects = Ui::ComputeChatBackgroundRects(
			size,
			source.size());
		const auto dpr = style::DevicePixelRatio();
		const auto full = rects.to.size() * dpr;
		const auto tiny = QSize(
			qMax(1, full.width() / 4),
			qMax(1, full.height() / 4));
		auto cover = source.copy(rects.from).scaled(
			tiny,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		cover.setDevicePixelRatio(1);
		cover = Images::BlurLargeImage(std::move(cover), 12);
		cover.setDevicePixelRatio(1);
		cover = std::move(cover).scaled(
			full,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		cover.setDevicePixelRatio(dpr);
		return cover;
	}

	QString _key;
	QSize _size;
	QImage _image;
};

} // namespace Dialogs
