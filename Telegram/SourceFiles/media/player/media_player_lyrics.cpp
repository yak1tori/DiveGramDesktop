#include "media/player/media_player_lyrics.h"

#include "media/player/media_player_instance.h"
#include "media/audio/media_audio.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "crl/crl.h"
#include "ui/painter.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "styles/style_media_player.h"

#include <QPainterPath>
#include <QLinearGradient>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QPair>
#include <QTextDocument>
#include <QTextBlock>
#include <ctime>

namespace Media::Player {
namespace {

constexpr auto kCardWidth = 1060;
constexpr auto kCardHeight = 620;
constexpr auto kCardRadius = 16;
constexpr auto kCoverSize = 220;
constexpr auto kCoverRadius = 12;
constexpr auto kPadding = 24;
constexpr auto kDimAlpha = 160;
constexpr auto kHighlightDuration = crl::time(250);
constexpr auto kScrollDuration = crl::time(300);
constexpr auto kLeftZone = 0.40;
constexpr auto kAppearDuration = crl::time(220);
constexpr auto kHideDuration = crl::time(180);
constexpr auto kSwitchFadeDuration = crl::time(200);
constexpr auto kAppearSlide = 16;
constexpr auto kHideSlide = 24;
constexpr auto kLineGap = 12;
constexpr auto kTextFontSize = 48;

QImage MakeCover(not_null<DocumentData*> document, int size) {
	QImage result(size, size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	QPainter p(&result);
	p.setRenderHint(QPainter::Antialiasing);

	const auto accent = st::mediaPlayerActiveFg->c;
	QLinearGradient g(0, 0, 0, size);
	g.setColorAt(0, accent);
	g.setColorAt(1, QColor(
		std::max(accent.red() - 60, 0),
		std::max(accent.green() - 60, 0),
		std::max(accent.blue() - 60, 0)));

	p.setBrush(g);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(QRectF(0, 0, size, size), kCoverRadius, kCoverRadius);

	p.setBrush(QColor(255, 255, 255, 200));
	QFont f(u"serif"_q);
	f.setPixelSize(size * 60 / 100);
	f.setWeight(QFont::Bold);
	p.setFont(f);
	p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, u"\u266B"_q);

	p.end();
	return result;
}

} // namespace

std::vector<LrcLine> ParseLrc(const QString &text) {
	std::vector<LrcLine> result;
	static const QRegularExpression tag(
		"\\[(\\d{1,2}):(\\d{2})(?:\\.(\\d{1,3}))?\\]");
	for (const auto &raw : text.split('\n')) {
		auto line = raw.trimmed();
		if (line.isEmpty()) continue;

		auto positions = std::vector<crl::time>();
		auto pos = 0;
		while (pos < line.size()) {
			const auto m = tag.match(line, pos);
			if (!m.hasMatch() || m.capturedStart() != pos) break;

			const auto mm = m.captured(1).toInt();
			const auto ss = m.captured(2).toInt();
			auto ms = 0;
			const auto msStr = m.captured(3);
			if (!msStr.isEmpty()) {
				ms = msStr.toInt();
				while (ms < 100) ms *= 10;
			}
			positions.push_back(
				mm * 60000LL + ss * 1000LL + ms);
			pos = m.capturedEnd();
		}
		auto content = line.mid(pos).trimmed();
		if (content.isEmpty()) continue;
		if (positions.empty()) {
			result.push_back({ -1, std::move(content) });
		} else {
			for (const auto &p : positions) {
				result.push_back({ p, content });
			}
		}
	}
	std::sort(
		result.begin(),
		result.end(),
		[](const LrcLine &a, const LrcLine &b) {
			if (a.positionMs == -1) return false;
			if (b.positionMs == -1) return true;
			return a.positionMs < b.positionMs;
		});
	return result;
}

void LyricsLog(const QString &line) {
	QFile f(u"/tmp/divegram_lyrics.log"_q);
	if (f.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
		f.write((QDateTime::currentDateTime().toString(u"HH:mm:ss.zzz "_q)
			+ line + u"\n"_q).toUtf8());
	}
}

QString HttpGetBytes(
		const QString &url,
		const QList<QPair<QString, QString>> &headers = {}) {
	LyricsLog(u"GET "_q + url);
	QStringList args = { u"-s"_q, u"-L"_q, u"--max-time"_q, u"12"_q,
		u"-A"_q, u"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/124.0 Safari/537.36"_q };
	for (const auto &[k, v] : headers) {
		args << u"-H"_q << (k + u": " + v);
	}
	args << url;
	QProcess proc;
	proc.start(u"curl"_q, args);
	if (!proc.waitForStarted(3000)) {
		LyricsLog(u"curl НЕ СТАРТОВАЛ"_q);
		return QString();
	}
	if (!proc.waitForFinished(15000)) {
		proc.kill();
		LyricsLog(u"curl ТАЙМАУТ"_q);
		return QString();
	}
	const auto data = proc.readAllStandardOutput();
	const auto err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
	LyricsLog(u"<- байт=%1 exit=%2 err=[%3]"_q
		.arg(data.size())
		.arg(proc.exitCode())
		.arg(err));
	return QString::fromUtf8(data);
}

QColor DominantCardColor(const QImage &cover) {
	if (cover.isNull()) {
		return st::menuBg->c;
	}
	const auto average = cover.scaled(
		1,
		1,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).pixelColor(0, 0);
	const auto hue = average.hslHue();
	const auto sat = std::min(average.hslSaturation(), 180);
	auto value = average.lightness();
	if (value > 150) {
		value = 150;
	} else if (value < 40) {
		value = 40;
	}
	return QColor::fromHsl(hue, sat, value);
}

struct LeftColumn {
	QRect coverRect;
	QRect titleRect;
	QRect artistRect;
	int buttonsY = 0;
	QRect timeRect;
	QRect barRect;
	int leftZone = 0;
	int leftCenterX = 0;
};

LeftColumn ComputeLeftColumn(const QRect &card) {
	LeftColumn result;
	result.leftZone = int(card.width() * kLeftZone);
	result.leftCenterX = card.left() + result.leftZone / 2;
	const auto leftX = result.leftCenterX - kCoverSize / 2;
	result.coverRect = QRect(
		leftX,
		card.top() + kPadding + 72,
		kCoverSize,
		kCoverSize);
	result.titleRect = QRect(
		leftX,
		result.coverRect.bottom() + 12,
		kCoverSize,
		24);
	result.artistRect = QRect(
		leftX,
		result.titleRect.bottom() + 4,
		kCoverSize,
		20);
	result.buttonsY = result.artistRect.bottom() + 20;
	const auto timeY = result.buttonsY + 36;
	result.timeRect = QRect(leftX, timeY, kCoverSize, 20);
	result.barRect = QRect(
		leftX,
		result.timeRect.bottom() + 8,
		kCoverSize,
		4);
	return result;
}

std::pair<QString, QString> TrackArtistTitle(not_null<DocumentData*> document) {
	const auto song = document->song();
	auto artist = song ? song->performer : QString();
	auto track = song ? song->title : QString();
	if (track.isEmpty()) {
		auto name = document->filename();
		for (const auto &ext : { u".m4a"_q, u".mp3"_q, u".ogg"_q, u".opus"_q }) {
			if (name.endsWith(ext, Qt::CaseInsensitive)) {
				name.chop(ext.size());
				break;
			}
		}
		name.remove(u"(youtube)"_q, Qt::CaseInsensitive);
		name = name.trimmed();
		const auto dash = name.lastIndexOf(u" - "_q);
		if (dash > 0) {
			if (artist.isEmpty()) artist = name.mid(0, dash).trimmed();
			track = name.mid(dash + 3).trimmed();
		} else {
			track = name;
		}
	}
	return { artist.trimmed(), track.trimmed() };
}

static QString UrlEncode(const QString &s) {
	return QString::fromUtf8(QUrl::toPercentEncoding(s));
}

QString FetchYandexLrc(const QString &artist, const QString &title) {
	LyricsLog(u"YANDEX: artist='"_q + artist + u"' title='"_q + title + u"'"_q);
	auto token = qEnvironmentVariable("YANDEX_ACCESS_TOKEN");
	if (token.isEmpty()) {
		const auto path = u"%1/.config/track_lyrics/yandex_token"_q;
		QFile f(path.arg(QDir::homePath()));
		if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
			token = QString::fromUtf8(f.readAll().trimmed());
		}
	}
	if (token.isEmpty()) {
		LyricsLog(u"YANDEX: ТОКЕН НЕ НАЙДЕН"_q);
		return QString();
	}
	LyricsLog(u"YANDEX: токен ok (%1 байт)"_q.arg(token.size()));

	QString searchUrl = u"https://api.music.yandex.net/search?text=%1&type=track&page=0"_q.arg(
		UrlEncode(artist + u" " + title));
	const auto searchJson = HttpGetBytes(searchUrl);
	LyricsLog(u"YANDEX: search ответ len="_q + QString::number(searchJson.size()));
	const auto searchDoc = QJsonDocument::fromJson(searchJson.toUtf8());
	if (!searchDoc.isObject()) return QString();
	const auto result = searchDoc.object().value(u"result"_q).toObject();
	const auto tracks = result.value(u"tracks"_q).toObject().value(u"results"_q).toArray();
	if (tracks.isEmpty()) {
		LyricsLog(u"YANDEX: треков НЕ найдено"_q);
		return QString();
	}
	LyricsLog(u"YANDEX: найдено треков "_q + QString::number(tracks.size()));

	QString bestId;
	int bestScore = 0;
	for (const auto &v : tracks) {
		const auto t = v.toObject();
		QStringList arts;
		for (const auto &a : t.value(u"artists"_q).toArray()) {
			arts << a.toObject().value(u"name"_q).toString();
		}
		auto pts = 0;
		if (t.value(u"title"_q).toString().toUpper() == title.toUpper()) pts += 2;
		const auto artsJoined = arts.join(u" ");
		const auto na = artist.toLower();
		const auto aa = artsJoined.toLower();
		if (!na.isEmpty() && (aa.contains(na) || na.contains(aa))) pts += 2;
		if (pts > bestScore) {
			bestScore = pts;
			bestId = t.value(u"id"_q).toString();
		}
	}
	if (bestId.isEmpty()) return QString();
	const auto tid = bestId.split(u"_"_q).constFirst();

	const auto ts = int(time(nullptr));
	const auto message = QString::number(tid.toInt()) + QString::number(ts);
	QMessageAuthenticationCode hmac(QCryptographicHash::Sha256);
	hmac.setKey(QByteArrayLiteral("p93jhgh689SBReK6ghtw62"));
	hmac.addData(message.toUtf8());
	const auto sign = QString::fromLatin1(hmac.result().toBase64());

	QString lrcUrl = u"https://api.music.yandex.net/tracks/%1/lyrics?format=LRC&timeStamp=%2&sign=%3"_q.arg(
		tid,
		QString::number(ts),
		UrlEncode(sign));
	const QList<QPair<QString, QString>> hdrs = {
		{ u"Authorization"_q, u"OAuth "_q + token },
		{ u"X-Yandex-Music-Client"_q, u"YandexMusicAndroid/24023621"_q },
	};
	LyricsLog(u"YANDEX: lrcUrl="_q + lrcUrl);
	const auto body = HttpGetBytes(lrcUrl, hdrs);
	LyricsLog(u"YANDEX: lrc ответ len="_q + QString::number(body.size()));
	const auto doc = QJsonDocument::fromJson(body.toUtf8());
	if (!doc.isObject()) return QString();
	const auto data = doc.object().value(u"result"_q).toObject();
	auto lrc = data.value(u"fullLyrics"_q).toString();
	if (lrc.isEmpty()) lrc = data.value(u"syncedLyrics"_q).toString();
	if (lrc.isEmpty()) {
		const auto dl = data.value(u"downloadUrl"_q).toString();
		if (!dl.isEmpty()) lrc = HttpGetBytes(dl);
	}
	lrc = lrc.trimmed();
	LyricsLog(u"YANDEX: итог len="_q + QString::number(lrc.size())
		+ u" превью='"_q + lrc.mid(0, 60) + u"'"_q);
	if (lrc.isEmpty()) return QString();
	if (lrc.contains(u"[") && lrc.contains(u"]")) return lrc;
	return QString();
}

QString FetchLrcLibLyrics(const QString &artist, const QString &title) {
	LyricsLog(u"LRCLIB: artist='"_q + artist + u"' title='"_q + title + u"'"_q);
	const auto u = u"https://lrclib.net/api/get?track_name=%1&artist_name=%2"_q.arg(
		UrlEncode(title),
		UrlEncode(artist));
	const auto doc = QJsonDocument::fromJson(HttpGetBytes(u).toUtf8());
	if (!doc.isObject()) return QString();
	const auto lrc = doc.object().value(u"syncedLyrics"_q).toString().trimmed();
	LyricsLog(u"LRCLIB: результат len="_q + QString::number(lrc.size()));
	return lrc;
}

QString FetchNeteaseLrc(const QString &artist, const QString &title) {
	const auto u = u"https://music.163.com/api/cloudsearch/pc?csrf_token=&s=%1&type=1&offset=0&limit=20"_q.arg(
		UrlEncode(artist + u" " + title));
	const auto body = HttpGetBytes(u);
	const auto doc = QJsonDocument::fromJson(body.toUtf8());
	if (!doc.isObject()) return QString();
	const auto songs = doc.object().value(u"result"_q).toObject().value(u"songs"_q).toArray();

	QStringList usable;
	for (const auto &v : songs) {
		const auto s = v.toObject();
		QStringList arts;
		for (const auto &a : s.value(u"artists"_q).toArray()) {
			arts << a.toObject().value(u"name"_q).toString();
		}
		auto pts = 0;
		if (s.value(u"name"_q).toString().toUpper() == title.toUpper()) pts += 2;
		if (s.value(u"name"_q).toString().toLower().contains(u"slow"_q)) pts -= 2;
		const auto na = artist.toLower();
		const auto aa = arts.join(u" ").toLower();
		if (!na.isEmpty() && (aa.contains(na) || na.contains(aa))) pts += 2;
		if (pts > 0) usable << s.value(u"id"_q).toString();
	}
	for (const auto &id : usable) {
		QUrl lu(u"https://music.163.com/api/song/lyric"_q);
		QUrlQuery lq;
		lq.addQueryItem(u"id"_q, id);
		lq.addQueryItem(u"lv"_q, u"1"_q);
		lq.addQueryItem(u"kv"_q, u"1"_q);
		lq.addQueryItem(u"tv"_q, u"-1"_q);
		lu.setQuery(lq);
		const auto lbody = HttpGetBytes(lu.toString());
		const auto ldoc = QJsonDocument::fromJson(lbody.toUtf8());
		if (!ldoc.isObject()) continue;
		auto lrc = ldoc.object().value(u"lrc"_q).toObject().value(u"lyric"_q).toString();
		QStringList keep;
		for (const auto &ln : lrc.split(u'\n')) {
			if (!ln.contains(u"作词"_q) && !ln.contains(u"作曲"_q)
				&& !ln.contains(u"编曲"_q) && !ln.contains(u"制作人"_q)) {
				keep << ln;
			}
		}
		lrc = keep.join(u'\n').trimmed();
		if (lrc.contains(u"[")) return lrc;
	}
	return QString();
}

QString FetchOvhLyrics(const QString &artist, const QString &title) {
	if (artist.isEmpty()) return QString();
	const auto url = u"https://api.lyrics.ovh/v1/%1/%2"_q.arg(
		QString(QUrl::toPercentEncoding(artist)),
		QString(QUrl::toPercentEncoding(title)));
	const auto doc = QJsonDocument::fromJson(HttpGetBytes(url).toUtf8());
	if (!doc.isObject()) return QString();
	return doc.object().value(u"lyrics"_q).toString().trimmed();
}

QString FetchSyncedLyrics(not_null<DocumentData*> document) {
	const auto [artist, track] = TrackArtistTitle(document);
	if (track.isEmpty()) return QString();

	const auto y = FetchYandexLrc(artist, track);
	if (!y.isEmpty()) return y;
	const auto l = FetchLrcLibLyrics(artist, track);
	if (!l.isEmpty()) return l;
	const auto n = FetchNeteaseLrc(artist, track);
	if (!n.isEmpty()) return n;
	return FetchOvhLyrics(artist, track);
}

bool HasTimedLines(const std::vector<LrcLine> &lines) {
	return std::any_of(lines.begin(), lines.end(), [](const LrcLine &l) {
		return l.positionMs >= 0;
	});
}

LyricsWidget::LyricsWidget(
	not_null<Window::SessionController*> controller,
	std::vector<LrcLine> &&lines,
	not_null<DocumentData*> document,
	FullMsgId contextId)
: Ui::LayerWidget()
, _controller(controller)
, _contextId(contextId)
, _lines(std::move(lines))
, _document(document)
, _documentMedia(document->createMediaView())
, _closeButton(this, st::mediaPlayerClose)
, _prevButton(this, st::mediaPlayerPreviousButton)
, _playPauseButton(this, st::mediaPlayerPlayButton)
, _nextButton(this, st::mediaPlayerNextButton) {
	setAttribute(Qt::WA_TransparentForMouseEvents, false);

	_textFont.setPixelSize(kTextFontSize);
	_textFont.setWeight(QFont::Bold);

	LyricsLog(u"LYRICS: конструктор, строк="_q + QString::number(_lines.size()));

	_documentMedia->thumbnailWanted(Data::FileOrigin(_contextId));
	_documentMedia->goodThumbnailWanted();

	rebuildCover();
	rebuildLineRects();
	_durationMs = _document->duration();

	if (_lines.empty()) {
		startLyricsFetch(_document);
	}

	_closeButton->setClickedCallback([=] { closeLayer(); });
	_closeButton->show();

	_prevButton->setClickedCallback([=] {
		instance()->previous();
		updateButtons();
	});
	_prevButton->show();

	_playPauseButton->setClickedCallback([=] {
		instance()->playPause();
		updateButtons();
	});
	_playPauseButton->show();

	_nextButton->setClickedCallback([=] {
		instance()->next();
		updateButtons();
	});
	_nextButton->show();

	_document->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (_coverLoaded) return;
		if (!(_documentMedia->goodThumbnail() || _documentMedia->thumbnail())) {
			return;
		}
		rebuildCover();
		update();
	}, lifetime());

	instance()->updatedNotifier(
	) | rpl::on_next([=](const TrackState &state) {
		if (state.id.contextId() != _contextId) return;
		const auto docDur = _document->duration();
		if (docDur > 0) {
			_durationMs = docDur;
		}
		_positionMs = (state.position * 1000LL) / state.frequency;
		updateHighlight(_positionMs);
		updateButtons();
		update();
	}, lifetime());

	instance()->trackChanged(
	) | rpl::on_next([=](AudioMsgId::Type type) {
		if (type != AudioMsgId::Type::Song) return;
		const auto current = instance()->current(AudioMsgId::Type::Song);
		const auto document = current.audio();
		if (!document) return;
		if (current.contextId() == _contextId) return;
		switchTo(current.contextId(), document);
	}, lifetime());

	show();

	_appearAnim.start(
		[=] { update(); },
		0.,
		1.,
		kAppearDuration,
		anim::sineInOut);
}

void LyricsWidget::startLyricsFetch(not_null<DocumentData*> document) {
	if (document != _document) return;
	_loading = true;
	_fetchFailed = false;
	crl::async([=, weak = QPointer<LyricsWidget>(this)] {
		const auto text = FetchSyncedLyrics(document);
		crl::on_main(weak, [=] {
			if (document != _document) return;
			_loading = false;
			auto parsed = ParseLrc(text);
			if (parsed.empty()) {
				_fetchFailed = true;
				update();
				return;
			}
			_lines = std::move(parsed);
			_scrollTop = 0;
			_followAnim.stop();
			rebuildLineRects();
			updateHighlight(_positionMs);
			update();
		});
	});
}

void LyricsWidget::switchTo(FullMsgId contextId, not_null<DocumentData*> document) {
	LyricsLog(u"LYRICS: switchTo "_q + document->filename());
	_contextId = contextId;
	_document = document;
	_lines.clear();
	_durationMs = document->duration();
	_positionMs = 0;
	_activeIndex = -1;
	_scrollTop = 0;
	_totalTextHeight = 0;
	_loading = false;
	_fetchFailed = false;
	_switchAnimRunning = true;
	_switchAnim.start([=] {
		if (_switchAnim.value(1.) >= 1.) {
			_switchAnimRunning = false;
		}
		update();
	}, 0., 1., kSwitchFadeDuration, anim::sineInOut);
	_documentMedia = document->createMediaView();
	_documentMedia->thumbnailWanted(Data::FileOrigin(_contextId));
	_documentMedia->goodThumbnailWanted();
	_coverLoaded = false;
	rebuildCover();
	rebuildLineRects();
	update();
	startLyricsFetch(document);
}

void LyricsWidget::Show(not_null<Window::SessionController*> controller) {
	const auto current = instance()->current(AudioMsgId::Type::Song);
	const auto document = current.audio();
	if (!document) {
		controller->showToast(QStringLiteral("Текст: нет аудио"));
		return;
	}
	const auto contextId = current.contextId();
	if (!contextId) {
		controller->showToast(QStringLiteral("Текст: нет контекста"));
		return;
	}

	const auto item = document->session().data().message(contextId);
	if (!item) {
		controller->showToast(
			QStringLiteral("Текст: item=null file=[%1] ctx=%2")
				.arg(document->filename())
				.arg(contextId.msg.bare));
		return;
	}

	const auto origText = item->originalText();
	auto text = origText.text.trimmed();
	auto lines = ParseLrc(text);
	LyricsLog(u"SHOW: открываю слой мгновенно, строк из подписи="_q
		+ QString::number(lines.size()));
	controller->showSpecialLayer(
		object_ptr<LyricsWidget>(
			controller,
			std::move(lines),
			document,
			contextId),
		anim::type::normal);
}

void LyricsWidget::rebuildCover() {
	_documentMedia->goodThumbnailWanted();
	if (const auto thumb = _documentMedia->goodThumbnail()) {
		const auto original = thumb->original();
		if (!original.isNull()) {
			_cover = original.scaled(
				kCoverSize,
				kCoverSize,
				Qt::KeepAspectRatioByExpanding,
				Qt::SmoothTransformation);
			_cardBg = DominantCardColor(_cover);
			_coverLoaded = true;
			return;
		}
	}
	if (const auto thumb = _documentMedia->thumbnail()) {
		const auto original = thumb->original();
		if (!original.isNull()) {
			_cover = original.scaled(
				kCoverSize,
				kCoverSize,
				Qt::KeepAspectRatioByExpanding,
				Qt::SmoothTransformation);
			_cardBg = DominantCardColor(_cover);
			_coverLoaded = true;
			return;
		}
	}
	_cover = MakeCover(_document, kCoverSize);
	_cardBg = DominantCardColor(_cover);
	_coverLoaded = false;
}

void LyricsWidget::rebuildLineRects() {
	_lineRects.clear();
	_totalTextHeight = 0;

	if (_lines.empty()) {
		return;
	}

	const auto card = cardRect();
	const auto geom = ComputeLeftColumn(card);
	const auto textAreaWidth = card.width() - geom.leftZone - kPadding * 2;
	if (textAreaWidth <= 0) return;

	for (const auto &line : _lines) {
		QTextDocument doc;
		doc.setDefaultFont(_textFont);
		doc.setPlainText(line.text);
		doc.setDocumentMargin(0);
		doc.setTextWidth(textAreaWidth);
		const auto h = int(doc.size().height());
		_lineRects.push_back({ _totalTextHeight, h });
		_totalTextHeight += h + kLineGap;
	}
	if (_totalTextHeight > kLineGap) {
		_totalTextHeight -= kLineGap;
	}
}

void LyricsWidget::updateHighlight(crl::time positionMs) {
	auto bestIdx = -1;
	for (auto i = 0, count = int(_lines.size()); i < count; ++i) {
		if (_lines[i].positionMs >= 0
			&& _lines[i].positionMs <= positionMs) {
			bestIdx = i;
		}
	}
	if (bestIdx == _activeIndex) return;
	const auto oldIdx = _activeIndex;
	_activeIndex = bestIdx;
	_activeColorAnim.start([=] { update(); },
		0.,
		1.,
		kHighlightDuration,
		anim::sineInOut);

	if (oldIdx < 0 || _activeIndex < 0) {
		return;
	}

	if (_activeIndex < int(_lineRects.size())) {
		const auto textAreaHeight = cardRect().height() - kPadding * 2;
		const auto lineTop = _lineRects[_activeIndex].top;
		const auto lineH = _lineRects[_activeIndex].height;
		const auto target = std::clamp(
			lineTop - (textAreaHeight - lineH) / 2,
			0,
			std::max(0, _totalTextHeight - textAreaHeight));
		_followAnim.start([=] {
			_scrollTop = int(std::round(_followAnim.value(double(target))));
			update();
		}, double(_scrollTop), double(target), kScrollDuration, anim::sineInOut);
	}
}

void LyricsWidget::seekToMs(crl::time ms) {
	if (_durationMs <= 0) return;
	const auto progress = std::clamp(
		double(ms) / double(_durationMs),
		0.,
		1.);
	_seeking = true;
	instance()->startSeeking(AudioMsgId::Type::Song);
	instance()->finishSeeking(AudioMsgId::Type::Song, progress);
	_seeking = false;
}

void LyricsWidget::closeLayer() {
	_closing = true;
	_closeAnim.start([=] {
		update();
		if (_closeAnim.value(0.) <= 0.01) {
			_controller->hideSpecialLayer(anim::type::instant);
		}
	}, 1., 0., kHideDuration, anim::sineInOut);
}

QRect LyricsWidget::cardRect() const {
	const auto w = std::min(kCardWidth, width() - 40);
	const auto h = std::min(kCardHeight, height() - 40);
	return QRect(
		(width() - w) / 2,
		(height() - h) / 2,
		w,
		h);
}

void LyricsWidget::parentResized() {
	if (const auto p = parentWidget()) {
		const auto ps = p->size();
		if (ps.width() > 0 && ps.height() > 0 && size() != ps) {
			resize(ps);
		}
	}
	const auto card = cardRect();
	const auto geom = ComputeLeftColumn(card);

	_closeButton->move(
		card.right() - _closeButton->width() - 12,
		card.top() + 12);

	const auto buttonsWidth
		= _prevButton->width()
		+ _playPauseButton->width()
		+ _nextButton->width();
	_prevButton->move(
		geom.leftCenterX - buttonsWidth / 2,
		geom.buttonsY);
	_playPauseButton->move(
		geom.leftCenterX - buttonsWidth / 2 + _prevButton->width(),
		geom.buttonsY);
	_nextButton->move(
		geom.leftCenterX - buttonsWidth / 2 + _prevButton->width()
			+ _playPauseButton->width(),
		geom.buttonsY);

	rebuildLineRects();
}

void LyricsWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	const auto appearValue = _appearAnim.value(1.);
	const auto closeValue = _closeAnim.value(1.);
	const auto switchValue = _switchAnimRunning
		? _switchAnim.value(1.)
		: 1.;
	const auto overall = (_closing ? (1. - closeValue) : appearValue)
		* switchValue;
	const auto slide = _closing
		? int((1. - appearValue) * (kHideSlide + kAppearSlide) + closeValue * kHideSlide)
		: int((1. - appearValue) * kAppearSlide);

	if (overall < 1.) {
		p.setOpacity(overall);
	}
	if (slide != 0) {
		p.translate(0, slide);
	}

	p.fillRect(rect(), QColor(0, 0, 0, int(kDimAlpha * (_closing ? (1. - closeValue) : 1.)));

	const auto card = cardRect();
	const auto geom = ComputeLeftColumn(card);

	p.setPen(Qt::NoPen);
	p.setBrush(_cardBg);
	p.drawRoundedRect(card, kCardRadius, kCardRadius);

	p.setClipPath([&] {
		QPainterPath path;
		path.addRoundedRect(geom.coverRect, kCoverRadius, kCoverRadius);
		return path;
	}());
	if (_cover.isNull()) {
		p.drawImage(geom.coverRect, _cover);
	} else {
		const auto s = std::min(_cover.width(), _cover.height());
		const auto src = QRect(
			(_cover.width() - s) / 2,
			(_cover.height() - s) / 2,
			s,
			s);
		p.drawImage(geom.coverRect, _cover, src);
	}
	p.setClipping(false);

	const auto composed = Ui::Text::FormatSongNameFor(
		_document
	).composedName();

	p.setPen(st::windowFg);
	QFont titleFont;
	titleFont.setWeight(QFont::Bold);
	titleFont.setPixelSize(18);
	p.setFont(titleFont);
	p.drawText(
		geom.titleRect,
		Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		composed.title);

	p.setPen(st::windowSubTextFg);
	QFont artistFont;
	artistFont.setWeight(QFont::Medium);
	artistFont.setPixelSize(14);
	p.setFont(artistFont);
	p.drawText(
		geom.artistRect,
		Qt::AlignHCenter | Qt::AlignTop,
		composed.performer);

	p.setPen(st::windowSubTextFg);
	QFont timeFont;
	timeFont.setPixelSize(14);
	p.setFont(timeFont);
	const auto posSec = _positionMs / 1000;
	const auto durSec = _durationMs / 1000;
	const auto timeText = Ui::FormatDurationText(posSec)
		+ u" / "_q
		+ Ui::FormatDurationText(durSec);
	p.drawText(
		geom.timeRect,
		Qt::AlignHCenter,
		timeText);

	const auto progress = _durationMs
		? double(_positionMs) / double(_durationMs)
		: 0.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::mediaPlayerInactiveFg);
	p.drawRoundedRect(geom.barRect, 2, 2);
	if (progress > 0) {
		p.setBrush(st::mediaPlayerActiveFg);
		p.drawRoundedRect(
			QRect(
				geom.barRect.x(),
				geom.barRect.y(),
				int(geom.barRect.width() * progress),
				geom.barRect.height()),
			2,
			2);
	}

	const auto textX = card.left() + geom.leftZone + kPadding;
	const auto textW = card.width() - geom.leftZone - kPadding * 2;
	const auto textY = card.top() + kPadding;
	const auto textH = card.height() - kPadding * 2;

	if (_lines.empty()) {
		QFont placeholderFont;
		placeholderFont.setPixelSize(22);
		p.setFont(placeholderFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(textX, textY, textW, textH),
			Qt::AlignCenter,
			_loading ? u"Ищем текст…"_q : u"Не удалось найти текст"_q);
		return;
	}

	if (_lineRects.size() != _lines.size()) {
		rebuildLineRects();
	}

	p.save();
	p.setClipRect(textX, textY, textW, textH);

	const auto value = _activeColorAnim.value(1.);
	const auto activeColor = anim::color(
		st::windowSubTextFg,
		st::mediaPlayerActiveFg,
		value);

	const auto count = std::min(
		int(_lines.size()),
		int(_lineRects.size()));
	for (auto i = 0; i < count; ++i) {
		const auto &[lineTop, lineH] = _lineRects[i];
		const auto screenY = textY + lineTop - _scrollTop;

		if (screenY + lineH < textY || screenY > textY + textH) {
			continue;
		}

		p.setPen((i == _activeIndex)
			? activeColor
			: st::windowSubTextFg->c);
		p.setFont(_textFont);
		p.drawText(
			QRect(textX, screenY, textW, lineH),
			Qt::AlignCenter | Qt::AlignVCenter | Qt::TextWordWrap,
			_lines[i].text);
	}

	p.restore();
}

void LyricsWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		closeLayer();
	} else {
		LayerWidget::keyPressEvent(e);
	}
}

void LyricsWidget::wheelEvent(QWheelEvent *e) {
	const auto textH = cardRect().height() - kPadding * 2;
	const auto maxScroll = std::max(0, _totalTextHeight - textH);

	_scrollTop -= e->angleDelta().y();
	_scrollTop = std::clamp(_scrollTop, 0, maxScroll);
	update();
	e->accept();
}

void LyricsWidget::mouseReleaseEvent(QMouseEvent *e) {
	const auto geom = ComputeLeftColumn(cardRect());
	if (geom.barRect.contains(e->pos()) && _durationMs > 0) {
		const auto x = e->pos().x() - geom.barRect.left();
		const auto ratio = std::clamp(
			double(x) / geom.barRect.width(),
			0.,
			1.);
		seekToMs(crl::time(ratio * _durationMs));
		return;
	}

	const auto card = cardRect();
	const auto leftGeom = ComputeLeftColumn(card);
	const auto textX = card.left() + leftGeom.leftZone + kPadding;
	const auto textW = card.width() - leftGeom.leftZone - kPadding * 2;
	const auto textY = card.top() + kPadding;

	const auto mx = e->pos().x() - textX;
	const auto my = e->pos().y() - textY + _scrollTop;
	if (mx >= 0 && mx <= textW) {
		for (auto i = 0, count = int(_lines.size()); i < count; ++i) {
			const auto &[lineTop, lineH] = _lineRects[i];
			if (my >= lineTop && my <= lineTop + lineH
				&& _lines[i].positionMs >= 0) {
				seekToMs(_lines[i].positionMs);
				break;
			}
		}
	}

	LayerWidget::mouseReleaseEvent(e);
}

void LyricsWidget::updateButtons() {
	const auto state = instance()->getState(AudioMsgId::Type::Song);
	const auto playing = IsStoppedOrStopping(state.state)
		? false
		: IsPausedOrPausing(state.state)
		? false
		: true;
	_playPauseButton->setIconOverride(
		playing ? &st::mediaPlayerPauseIcon : nullptr);
	_prevButton->setDisabled(
		!instance()->previousAvailable(AudioMsgId::Type::Song));
	_nextButton->setDisabled(
		!instance()->nextAvailable(AudioMsgId::Type::Song));
}

} // namespace Media::Player
