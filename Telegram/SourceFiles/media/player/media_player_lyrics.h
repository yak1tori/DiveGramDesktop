#pragma once

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_types.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

class DocumentData;

namespace Window {
class SessionController;
} // namespace Window

namespace Media::Player {

struct LrcLine {
	crl::time positionMs = -1;
	QString text;
};

[[nodiscard]] std::vector<LrcLine> ParseLrc(const QString &text);

[[nodiscard]] bool HasTimedLines(const std::vector<LrcLine> &lines);

class LyricsWidget final : public Ui::LayerWidget {
public:
	static void Show(not_null<Window::SessionController*> controller);

	LyricsWidget(
		not_null<Window::SessionController*> controller,
		std::vector<LrcLine> &&lines,
		not_null<DocumentData*> document,
		FullMsgId contextId);

protected:
	void parentResized() override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void rebuildCover();
	void rebuildLineRects();
	void updateHighlight(crl::time positionMs);
	void seekToMs(crl::time ms);
	void closeLayer();
	void updateButtons();
	void startLyricsFetch(not_null<DocumentData*> document);
	void switchTo(FullMsgId contextId, not_null<DocumentData*> document);

	[[nodiscard]] QRect cardRect() const;

	const not_null<Window::SessionController*> _controller;
	FullMsgId _contextId;
	std::vector<LrcLine> _lines;
	DocumentData *_document = nullptr;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;

	QImage _cover;
	QColor _cardBg;
	bool _coverLoaded = false;

	object_ptr<Ui::IconButton> _closeButton = { nullptr };
	object_ptr<Ui::IconButton> _prevButton = { nullptr };
	object_ptr<Ui::IconButton> _playPauseButton = { nullptr };
	object_ptr<Ui::IconButton> _nextButton = { nullptr };

	int _activeIndex = -1;
	crl::time _durationMs = 0;
	crl::time _positionMs = 0;
	bool _seeking = false;
	bool _loading = false;
	bool _fetchFailed = false;

	int _scrollTop = 0;
	int _totalTextHeight = 0;
	QFont _textFont;

	struct LineRect {
		int top = 0;
		int height = 0;
	};
	std::vector<LineRect> _lineRects;

	Ui::Animations::Simple _appearAnim;
	Ui::Animations::Simple _closeAnim;
	Ui::Animations::Simple _switchAnim;
	bool _closing = false;
	bool _switchAnimRunning = false;

	Ui::Animations::Simple _followAnim;
	Ui::Animations::Simple _activeColorAnim;
	Ui::Animations::Simple _appearAnim;
	Ui::Animations::Simple _closeAnim;
	Ui::Animations::Simple _switchAnim;
	bool _closing = false;
	bool _switchAnimating = false;

	rpl::lifetime _lifetime;

};

} // namespace Media::Player
