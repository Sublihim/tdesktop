/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/background_preview_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "window/themes/window_theme.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/widgets/checkbox.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/history_view_message.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "boxes/confirm_box.h"
#include "boxes/background_preview_box.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMaxWallPaperSlugLength = 255;

class ServiceCheck : public Ui::AbstractCheckView {
public:
	ServiceCheck(const style::ServiceCheck &st, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	class Generator {
	public:
		Generator();

		void paintFrame(
			Painter &p,
			int left,
			int top,
			not_null<const style::ServiceCheck*> st,
			float64 toggled);
		void invalidate();

	private:
		struct Frames {
			QImage image;
			std::vector<bool> ready;
		};

		not_null<Frames*> framesForStyle(
			not_null<const style::ServiceCheck*> st);
		static void FillFrame(
			QImage &image,
			not_null<const style::ServiceCheck*> st,
			int index,
			int count);
		static void PaintFillingFrame(
			Painter &p,
			not_null<const style::ServiceCheck*> st,
			float64 progress);
		static void PaintCheckingFrame(
			Painter &p,
			not_null<const style::ServiceCheck*> st,
			float64 progress);

		base::flat_map<not_null<const style::ServiceCheck*>, Frames> _data;
		rpl::lifetime _lifetime;

	};
	static Generator &Frames();

	const style::ServiceCheck &_st;

};

ServiceCheck::Generator::Generator() {
	*_lifetime.make_state<base::Subscription>() = Window::Theme::Background(
	)->add_subscription([=](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			invalidate();
		}
	});
}

auto ServiceCheck::Generator::framesForStyle(
		not_null<const style::ServiceCheck*> st) -> not_null<Frames*> {
	if (const auto i = _data.find(st); i != _data.end()) {
		return &i->second;
	}
	const auto result = &_data.emplace(st, Frames()).first->second;
	const auto size = st->diameter;
	const auto count = (st->duration / AnimationTimerDelta) + 2;
	result->image = QImage(
		QSize(count * size, size) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result->image.fill(Qt::transparent);
	result->image.setDevicePixelRatio(cRetinaFactor());
	result->ready.resize(count);
	return result;
}

void ServiceCheck::Generator::FillFrame(
		QImage &image,
		not_null<const style::ServiceCheck*> st,
		int index,
		int count) {
	Expects(count > 1);
	Expects(index >= 0 && index < count);

	Painter p(&image);
	PainterHighQualityEnabler hq(p);

	p.translate(index * st->diameter, 0);
	const auto progress = index / float64(count - 1);
	if (progress > 0.5) {
		PaintCheckingFrame(p, st, (progress - 0.5) * 2);
	} else {
		PaintFillingFrame(p, st, progress * 2);
	}
}

void ServiceCheck::Generator::PaintFillingFrame(
		Painter &p,
		not_null<const style::ServiceCheck*> st,
		float64 progress) {
	const auto shift = progress * st->shift;
	p.setBrush(st->color);
	p.setPen(Qt::NoPen);
	p.drawEllipse(QRectF(
		shift,
		shift,
		st->diameter - 2 * shift,
		st->diameter - 2 * shift));
	if (progress < 1.) {
		const auto remove = progress * (st->diameter / 2. - st->thickness);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		p.drawEllipse(QRectF(
			st->thickness + remove,
			st->thickness + remove,
			st->diameter - 2 * (st->thickness + remove),
			st->diameter - 2 * (st->thickness + remove)));
	}
}

void ServiceCheck::Generator::PaintCheckingFrame(
		Painter &p,
		not_null<const style::ServiceCheck*> st,
		float64 progress) {
	const auto shift = (1. - progress) * st->shift;
	p.setBrush(st->color);
	p.setPen(Qt::NoPen);
	p.drawEllipse(QRectF(
		shift,
		shift,
		st->diameter - 2 * shift,
		st->diameter - 2 * shift));
	if (progress > 0.) {
		const auto tip = QPointF(st->tip.x(), st->tip.y());
		const auto left = tip - QPointF(st->small, st->small) * progress;
		const auto right = tip - QPointF(-st->large, st->large) * progress;

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(Qt::NoBrush);
		auto pen = QPen(Qt::transparent);
		pen.setWidth(st->stroke);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		auto path = QPainterPath();
		path.moveTo(left);
		path.lineTo(tip);
		path.lineTo(right);
		p.drawPath(path);
	}
}

void ServiceCheck::Generator::paintFrame(
		Painter &p,
		int left,
		int top,
		not_null<const style::ServiceCheck*> st,
		float64 toggled) {
	const auto frames = framesForStyle(st);
	auto &image = frames->image;
	const auto count = int(frames->ready.size());
	const auto index = int(std::round(toggled * (count - 1)));
	Assert(index >= 0 && index < count);
	if (!frames->ready[index]) {
		frames->ready[index] = true;
		FillFrame(image, st, index, count);
	}
	const auto size = st->diameter;
	const auto part = size * cIntRetinaFactor();
	p.drawImage(
		QPoint(left, top),
		image,
		QRect(index * part, 0, part, part));
}

void ServiceCheck::Generator::invalidate() {
	_data.clear();
}

ServiceCheck::Generator &ServiceCheck::Frames() {
	static const auto Instance = Ui::CreateChild<Generator>(
		QApplication::instance());
	return *Instance;
}

ServiceCheck::ServiceCheck(
	const style::ServiceCheck &st,
	bool checked)
: AbstractCheckView(st.duration, checked, nullptr)
, _st(st) {
}

QSize ServiceCheck::getSize() const {
	const auto inner = QRect(0, 0, _st.diameter, _st.diameter);
	return inner.marginsAdded(_st.margin).size();
}

void ServiceCheck::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) {
	Frames().paintFrame(
		p,
		left + _st.margin.left(),
		top + _st.margin.top(),
		&_st,
		currentAnimationValue(ms));
}

QImage ServiceCheck::prepareRippleMask() const {
	return QImage();
}

bool ServiceCheck::checkRippleStartPosition(QPoint position) const {
	return false;
}

[[nodiscard]] bool IsValidWallPaperSlug(const QString &slug) {
	if (slug.isEmpty() || slug.size() > kMaxWallPaperSlugLength) {
		return false;
	}
	return ranges::find_if(slug, [](QChar ch) {
		return (ch != '.')
			&& (ch != '_')
			&& (ch != '-')
			&& (ch < '0' || ch > '9')
			&& (ch < 'a' || ch > 'z')
			&& (ch < 'A' || ch > 'Z');
	}) == slug.end();
}

AdminLog::OwnedItem GenerateTextItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const QString &text,
		bool out) {
	Expects(history->peer->isUser());

	using Flag = MTPDmessage::Flag;
	static auto id = ServerMaxMsgId + (ServerMaxMsgId / 3);
	const auto flags = Flag::f_entities
		| Flag::f_from_id
		| (out ? Flag::f_out : Flag(0));
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto item = new HistoryMessage(
		history,
		++id,
		flags,
		replyTo,
		viaBotId,
		unixtime(),
		out ? history->session().userId() : peerToUser(history->peer->id),
		QString(),
		TextWithEntities{ TextUtilities::Clean(text) });
	return AdminLog::OwnedItem(delegate, item);
}

QImage PrepareScaledNonPattern(
		const QImage &image,
		Images::Option blur) {
	const auto size = st::boxWideWidth;
	const auto width = std::max(image.width(), 1);
	const auto height = std::max(image.height(), 1);
	const auto takeWidth = (width > height)
		? (width * size / height)
		: size;
	const auto takeHeight = (width > height)
		? size
		: (height * size / width);
	return Images::prepare(
		image,
		takeWidth * cIntRetinaFactor(),
		takeHeight * cIntRetinaFactor(),
		Images::Option::Smooth
		| Images::Option::TransparentBackground
		| blur,
		size,
		size);
}

QImage ColorizePattern(QImage image, QColor color) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	// Similar to style::colorizeImage.
	// But style::colorizeImage takes pattern with all pixels having the
	// same components value, from (0, 0, 0, 0) to (255, 255, 255, 255).
	//
	// While in patterns we have different value ranges, usually they are
	// from (0, 0, 0, 0) to (0, 0, 0, 255), so we should use only 'alpha'.

	const auto width = image.width();
	const auto height = image.height();
	const auto pattern = anim::shifted(color);

	const auto resultBytesPerPixel = (image.depth() >> 3);
	constexpr auto resultIntsPerPixel = 1;
	const auto resultIntsPerLine = (image.bytesPerLine() >> 2);
	const auto resultIntsAdded = resultIntsPerLine - width * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(image.bits());
	Assert(resultIntsAdded >= 0);
	Assert(image.depth() == static_cast<int>((resultIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (resultIntsPerLine << 2));

	const auto maskBytesPerPixel = (image.depth() >> 3);
	const auto maskBytesPerLine = image.bytesPerLine();
	const auto maskBytesAdded = maskBytesPerLine - width * maskBytesPerPixel;

	// We want to read the last byte of four available.
	// This is the difference with style::colorizeImage.
	auto maskBytes = image.constBits() + (maskBytesPerPixel - 1);
	Assert(maskBytesAdded >= 0);
	Assert(image.depth() == (maskBytesPerPixel << 3));
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			auto maskOpacity = static_cast<anim::ShiftedMultiplier>(*maskBytes) + 1;
			*resultInts = anim::unshifted(pattern * maskOpacity);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}
	return image;
}

QImage PrepareScaledFromFull(
		const QImage &image,
		std::optional<QColor> patternBackground,
		Images::Option blur = Images::Option(0)) {
	auto result = PrepareScaledNonPattern(image, blur);
	if (patternBackground) {
		result = ColorizePattern(
			std::move(result),
			Data::PatternColor(*patternBackground));
	}
	return std::move(result).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
}

} // namespace

BackgroundPreviewBox::BackgroundPreviewBox(
	QWidget*,
	const Data::WallPaper &paper)
: _text1(GenerateTextItem(
	this,
	Auth().data().history(peerFromUser(ServiceUserId)),
	lang(lng_background_text1),
	false))
, _text2(GenerateTextItem(
	this,
	Auth().data().history(peerFromUser(ServiceUserId)),
	lang(lng_background_text2),
	true))
, _paper(paper)
, _radial(animation(this, &BackgroundPreviewBox::step_radial)) {
	subscribe(Auth().downloaderTaskFinished(), [=] { update(); });
}

void BackgroundPreviewBox::prepare() {
	setTitle(langFactory(lng_background_header));

	addButton(langFactory(lng_background_apply), [=] { apply(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
	if (_paper.hasShareUrl()) {
		addLeftButton(langFactory(lng_background_share), [=] { share(); });
	}
	updateServiceBg(_paper.backgroundColor());

	_paper.loadThumbnail();
	_paper.loadDocument();
	if (_paper.document() && _paper.document()->loading()) {
		_radial.start(_paper.document()->progress());
	}
	if (_paper.thumbnail() && !_paper.isPattern()) {
		createBlurCheckbox();
	}
	setScaledFromThumb();
	checkLoadedDocument();

	_text1->setDisplayDate(true);
	_text1->initDimensions();
	_text1->resizeGetHeight(st::boxWideWidth);
	_text2->initDimensions();
	_text2->resizeGetHeight(st::boxWideWidth);

	setDimensions(st::boxWideWidth, st::boxWideWidth);
}

void BackgroundPreviewBox::createBlurCheckbox() {
	_blur.create(
		this,
		lang(lng_background_blur),
		st::backgroundCheckbox,
		std::make_unique<ServiceCheck>(
			st::backgroundCheck,
			_paper.isBlurred()));

	rpl::combine(
		sizeValue(),
		_blur->sizeValue()
	) | rpl::start_with_next([=](QSize outer, QSize inner) {
		_blur->move(
			(outer.width() - inner.width()) / 2,
			outer.height() - st::historyPaddingBottom - inner.height());
	}, _blur->lifetime());

	_blur->paintRequest(
	) | rpl::filter([=] {
		return _serviceBg.has_value();
	}) | rpl::start_with_next([=] {
		Painter p(_blur.data());
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(*_serviceBg);
		p.drawRoundedRect(
			_blur->rect(),
			st::historyMessageRadius,
			st::historyMessageRadius);
	}, _blur->lifetime());

	_blur->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		checkBlurAnimationStart();
		update();
	}, lifetime());

	_blur->setDisabled(true);
}

void BackgroundPreviewBox::apply() {
	const auto install = (_paper.id() != Window::Theme::Background()->id())
		&& Data::IsCloudWallPaper(_paper);
	App::main()->setChatBackground(_paper, std::move(_full));
	if (install) {
		Auth().api().request(MTPaccount_InstallWallPaper(
			_paper.mtpInput(),
			_paper.mtpSettings()
		)).send();
	}
	closeBox();
}

void BackgroundPreviewBox::share() {
	QApplication::clipboard()->setText(_paper.shareUrl());
	Ui::Toast::Show(lang(lng_background_link_copied));
}

void BackgroundPreviewBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	const auto color = _paper.backgroundColor();
	if (color) {
		p.fillRect(e->rect(), *color);
	}
	if (!color || _paper.isPattern()) {
		if (!_scaled.isNull() || setScaledFromThumb()) {
			paintImage(p, ms);
			paintRadial(p, ms);
		} else if (!color) {
			p.fillRect(e->rect(), st::boxBg);
			return;
		} else {
			// Progress of pattern loading.
			paintRadial(p, ms);
		}
	}
	paintTexts(p, ms);
}

void BackgroundPreviewBox::paintImage(Painter &p, TimeMs ms) {
	Expects(!_scaled.isNull());

	const auto master = _paper.isPattern()
		? std::clamp(_paper.patternIntensity() / 100., 0., 1.)
		: 1.;

	const auto factor = cIntRetinaFactor();
	const auto size = st::boxWideWidth;
	const auto from = QRect(
		0,
		(size - height()) / 2 * factor,
		size * factor,
		height() * factor);
	const auto guard = gsl::finally([&] { p.setOpacity(1.); });

	const auto fade = _fadeIn.current(ms, 1.);
	if (fade < 1. && !_fadeOutThumbnail.isNull()) {
		p.drawPixmap(rect(), _fadeOutThumbnail, from);
	}
	const auto &pixmap = (!_blurred.isNull() && _paper.isBlurred())
		? _blurred
		: _scaled;
	p.setOpacity(master * fade);
	p.drawPixmap(rect(), pixmap, from);
	checkBlurAnimationStart();
}

void BackgroundPreviewBox::paintRadial(Painter &p, TimeMs ms) {
	bool radial = false;
	float64 radialOpacity = 0;
	if (_radial.animating()) {
		_radial.step(ms);
		radial = _radial.animating();
		radialOpacity = _radial.opacity();
	}
	if (!radial) {
		return;
	}
	auto inner = radialRect();

	p.setPen(Qt::NoPen);
	p.setOpacity(radialOpacity);
	p.setBrush(st::radialBg);

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	p.setOpacity(1);
	QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
	_radial.draw(p, arc, st::radialLine, st::radialFg);
}

int BackgroundPreviewBox::textsTop() const {
	const auto bottom = _blur ? _blur->y() : height();
	return bottom
		- st::historyPaddingBottom
		- _text1->height()
		- _text2->height();
}

QRect BackgroundPreviewBox::radialRect() const {
	const auto available = textsTop() - st::historyPaddingBottom;
	return QRect(
		QPoint(
			(width() - st::radialSize.width()) / 2,
			(available - st::radialSize.height()) / 2),
		st::radialSize);
}

void BackgroundPreviewBox::paintTexts(Painter &p, TimeMs ms) {
	const auto height1 = _text1->height();
	const auto height2 = _text2->height();
	p.translate(0, textsTop());
	paintDate(p);
	_text1->draw(p, rect(), TextSelection(), ms);
	p.translate(0, height1);
	_text2->draw(p, rect(), TextSelection(), ms);
	p.translate(0, height2);
}

void BackgroundPreviewBox::paintDate(Painter &p) {
	const auto date = _text1->Get<HistoryView::DateBadge>();
	if (!date || !_serviceBg) {
		return;
	}
	const auto text = date->text;
	const auto bubbleHeight = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	const auto bubbleTop = st::msgServiceMargin.top();
	const auto textWidth = st::msgServiceFont->width(text);
	const auto bubbleWidth = st::msgServicePadding.left() + textWidth + st::msgServicePadding.right();
	const auto bubbleLeft = (width() - bubbleWidth) / 2;
	const auto radius = bubbleHeight / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(*_serviceBg);
	p.drawRoundedRect(bubbleLeft, bubbleTop, bubbleWidth, bubbleHeight, radius, radius);
	p.setPen(st::msgServiceFg);
	p.setFont(st::msgServiceFont);
	p.drawText(bubbleLeft + st::msgServicePadding.left(), bubbleTop + st::msgServicePadding.top() + st::msgServiceFont->ascent, text);
}

void BackgroundPreviewBox::step_radial(TimeMs ms, bool timer) {
	Expects(_paper.document() != nullptr);

	const auto document = _paper.document();
	const auto wasAnimating = _radial.animating();
	const auto updated = _radial.update(
		document->progress(),
		!document->loading(),
		ms);
	if (timer
		&& (wasAnimating || _radial.animating())
		&& (!anim::Disabled() || updated)) {
		update(radialRect());
	}
	checkLoadedDocument();
}

bool BackgroundPreviewBox::setScaledFromThumb() {
	const auto thumbnail = _paper.thumbnail();
	if (!thumbnail || !thumbnail->loaded()) {
		return false;
	} else if (_paper.isPattern() && _paper.document() != nullptr) {
		return false;
	}
	auto scaled = PrepareScaledFromFull(
		thumbnail->original(),
		patternBackgroundColor(),
		_paper.document() ? Images::Option::Blurred : Images::Option(0));
	auto blurred = (_paper.document() || _paper.isPattern())
		? QImage()
		: PrepareScaledNonPattern(
			Data::PrepareBlurredBackground(thumbnail->original()),
			Images::Option(0));
	setScaledFromImage(std::move(scaled), std::move(blurred));
	return true;
}

void BackgroundPreviewBox::setScaledFromImage(
		QImage &&image,
		QImage &&blurred) {
	updateServiceBg(Window::Theme::CountAverageColor(image));
	if (!_full.isNull()) {
		startFadeInFrom(std::move(_scaled));
	}
	_scaled = App::pixmapFromImageInPlace(std::move(image));
	_blurred = App::pixmapFromImageInPlace(std::move(blurred));
	if (_blur && (!_paper.document() || !_full.isNull())) {
		_blur->setDisabled(false);
	}
}

void BackgroundPreviewBox::startFadeInFrom(QPixmap previous) {
	_fadeOutThumbnail = std::move(previous);
	_fadeIn.start([=] { update(); }, 0., 1., st::backgroundCheck.duration);
}

void BackgroundPreviewBox::checkBlurAnimationStart() {
	if (_fadeIn.animating()
		|| _blurred.isNull()
		|| !_blur
		|| _paper.isBlurred() == _blur->checked()) {
		return;
	}
	_paper = _paper.withBlurred(_blur->checked());
	startFadeInFrom(_paper.isBlurred() ? _scaled : _blurred);
}

void BackgroundPreviewBox::updateServiceBg(std::optional<QColor> background) {
	if (background) {
		_serviceBg = Window::Theme::AdjustedColor(
			st::msgServiceBg->c,
			*background);
	}
}

std::optional<QColor> BackgroundPreviewBox::patternBackgroundColor() const {
	return _paper.isPattern() ? _paper.backgroundColor() : std::nullopt;
}

void BackgroundPreviewBox::checkLoadedDocument() {
	const auto document = _paper.document();
	if (!_full.isNull()
		|| !document
		|| !document->loaded(DocumentData::FilePathResolveChecked)
		|| _generating) {
		return;
	}
	const auto generateCallback = [=](QImage &&image) {
		auto [left, right] = base::make_binary_guard();
		_generating = std::move(left);
		crl::async([
			this,
			image = std::move(image),
			patternBackground = patternBackgroundColor(),
			guard = std::move(right)
		]() mutable {
			auto scaled = PrepareScaledFromFull(image, patternBackground);
			const auto ms = getms();
			auto blurred = patternBackground
				? QImage()
				: PrepareScaledNonPattern(
					Data::PrepareBlurredBackground(image),
					Images::Option(0));
			crl::on_main([
				this,
				image = std::move(image),
				scaled = std::move(scaled),
				blurred = std::move(blurred),
				guard = std::move(guard)
			]() mutable {
				if (!guard) {
					return;
				}
				_full = std::move(image);
				setScaledFromImage(std::move(scaled), std::move(blurred));
				update();
			});
		});
	};
	_generating = Data::ReadImageAsync(
		document,
		Window::Theme::ProcessBackgroundImage,
		generateCallback);
}

bool BackgroundPreviewBox::Start(
		const QString &slug,
		const QMap<QString, QString> &params) {
	if (const auto paper = Data::WallPaper::FromColorSlug(slug)) {
		Ui::show(Box<BackgroundPreviewBox>(paper->withUrlParams(params)));
		return true;
	}
	if (!IsValidWallPaperSlug(slug)) {
		Ui::show(Box<InformBox>(lang(lng_background_bad_link)));
		return false;
	}
	Auth().api().requestWallPaper(slug, [=](const Data::WallPaper &result) {
		Ui::show(Box<BackgroundPreviewBox>(result.withUrlParams(params)));
	}, [](const RPCError &error) {
		Ui::show(Box<InformBox>(lang(lng_background_bad_link)));
	});
	return true;
}

HistoryView::Context BackgroundPreviewBox::elementContext() {
	return HistoryView::Context::ContactPreview;
}

std::unique_ptr<HistoryView::Element> BackgroundPreviewBox::elementCreate(
		not_null<HistoryMessage*> message) {
	return std::make_unique<HistoryView::Message>(this, message);
}

std::unique_ptr<HistoryView::Element> BackgroundPreviewBox::elementCreate(
		not_null<HistoryService*> message) {
	Unexpected("Service message in BackgroundPreviewBox.");
}

bool BackgroundPreviewBox::elementUnderCursor(
		not_null<const Element*> view) {
	return false;
}

void BackgroundPreviewBox::elementAnimationAutoplayAsync(
	not_null<const Element*> element) {
}

TimeMs BackgroundPreviewBox::elementHighlightTime(
		not_null<const Element*> element) {
	return TimeMs();
}

bool BackgroundPreviewBox::elementInSelectionMode() {
	return false;
}
