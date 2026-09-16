#include "divegram/ui/settings/settings_divegram_sections.h"

#include "divegram/ui/settings/settings_divegram.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "base/variant.h"
#include "core/version.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;

namespace {

void AddDescription(
		not_null<Ui::VerticalLayout*> container,
		const QString &text) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(text),
			st::boxDividerLabel),
		st::defaultBoxDividerLabelPadding);
}

not_null<Button*> AddSettingToggleWithDescription(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const QString &description,
		BoolGetter getter,
		BoolSetter setter) {
	const auto result = AddSettingToggle(container, std::move(text), getter, setter);
	AddDescription(container, description);
	return result;
}

not_null<Button*> AddToggleWithDescription(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const QString &description,
		Fn<bool()> getter,
		Fn<void(bool)> setter) {
	const auto result = AddToggle(container, std::move(text), getter, setter);
	AddDescription(container, description);
	return result;
}

const auto kGeneralMeta = BuildHelper({
	.id = DiveGramGeneral::Id(),
	.parentId = DiveGramMain::Id(),
	.title = QString("General"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(QString("Ghost Mode and Anti-Recall settings for DiveGram.")),
				st::boxLabel),
			.align = style::al_top,
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [](const SearchContext &) {
		}, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			const auto session = &wctx.controller->session();

			AddSubsectionTitle(container, rpl::single(QString("Ghost Mode")));

			auto &ghost = AyuSettings::ghost(session);

			AddToggleWithDescription(
				container,
				rpl::single(QString("Ghost Mode Active")),
				QString("Master switch: DiveGram stops reporting your presence "
					"to Telegram servers. Nothing marks you as reading, typing "
					"or online."),
				[&ghost] { return ghost.isGhostModeActive(); },
				[&ghost](bool v) { ghost.setGhostModeEnabled(v); });

			AddCollapsibleToggle(
				container,
				rpl::single(QString("Hide Activity")),
				std::vector<NestedEntry>{
					NestedEntry{
						QString("Don't send read receipts"),
						[&ghost] { return !ghost.sendReadMessages(); },
						[&ghost](bool v) { ghost.setSendReadMessages(!v); },
						[&ghost] { return ghost.sendReadMessagesLocked(); },
						[&ghost](bool v) { ghost.setSendReadMessagesLocked(v); },
					},
					NestedEntry{
						QString("Don't send read stories"),
						[&ghost] { return !ghost.sendReadStories(); },
						[&ghost](bool v) { ghost.setSendReadStories(!v); },
						[&ghost] { return ghost.sendReadStoriesLocked(); },
						[&ghost](bool v) { ghost.setSendReadStoriesLocked(v); },
					},
					NestedEntry{
						QString("Don't show online status"),
						[&ghost] { return !ghost.sendOnlinePackets(); },
						[&ghost](bool v) { ghost.setSendOnlinePackets(!v); },
						[&ghost] { return ghost.sendOnlinePacketsLocked(); },
						[&ghost](bool v) { ghost.setSendOnlinePacketsLocked(v); },
					},
					NestedEntry{
						QString("Don't broadcast upload progress"),
						[&ghost] { return !ghost.sendUploadProgress(); },
						[&ghost](bool v) { ghost.setSendUploadProgress(!v); },
						[&ghost] { return ghost.sendUploadProgressLocked(); },
						[&ghost](bool v) { ghost.setSendUploadProgressLocked(v); },
					},
					NestedEntry{
						QString("Send offline packet after activity"),
						[&ghost] { return ghost.sendOfflinePacketAfterOnline(); },
						[&ghost](bool v) { ghost.setSendOfflinePacketAfterOnline(v); },
						[&ghost] { return ghost.sendOfflinePacketAfterOnlineLocked(); },
						[&ghost](bool v) { ghost.setSendOfflinePacketAfterOnlineLocked(v); },
					},
				},
				true,
				QString("Select exactly which traces of activity are hidden. "
					"Hold Shift and click a checkbox to pin it for the "
					"selected chat only."));

			AddToggleWithDescription(
				container,
				rpl::single(QString("Mark messages read after actions")),
				QString("After you forward, copy or save a message, it is "
					"silently marked as read so no unread badge appears."),
				[&ghost] { return ghost.markReadAfterAction(); },
				[&ghost](bool v) { ghost.setMarkReadAfterAction(v); });

			AddToggleWithDescription(
				container,
				rpl::single(QString("Suggest ghost mode before viewing stories")),
				QString("Before you open someone's stories DiveGram offers "
					"to enable ghost mode so the author never sees your "
					"view."),
				[&ghost] { return ghost.suggestGhostModeBeforeViewingStory(); },
				[&ghost](bool v) { ghost.setSuggestGhostModeBeforeViewingStory(v); });

			AddSkip(container);
			AddDivider(container);
			AddSkip(container);

			AddSubsectionTitle(container, rpl::single(QString("Anti-Recall")));

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Save deleted messages")),
				QString("Deleted messages are kept in the chat and marked "
					"with a tombstone instead of vanishing forever."),
				&AyuSettings::saveDeletedMessages,
				&AyuSettings::setSaveDeletedMessages);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Show deleted messages semi-transparent")),
				QString("Render saved deleted messages at reduced opacity "
					"so you always see what was hidden from you."),
				&AyuSettings::semiTransparentDeletedMessages,
				&AyuSettings::setSemiTransparentDeletedMessages);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Store full message history")),
				QString("Keep every revision of edited messages so you can "
					"open the edit history of any message later."),
				&AyuSettings::saveMessagesHistory,
				&AyuSettings::setSaveMessagesHistory);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Save messages from bots")),
				QString("Anti-Recall also applies to conversations with "
					"bots, not only to regular chats."),
				&AyuSettings::saveForBots,
				&AyuSettings::setSaveForBots);
		});
	});
});

} // namespace

rpl::producer<QString> DiveGramGeneral::title() {
	return rpl::single(QString("General"));
}

DiveGramGeneral::DiveGramGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void DiveGramGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kGeneralMeta.build);
	Ui::ResizeFitChild(this, content);
}

namespace {

const auto kAppearanceMeta = BuildHelper({
	.id = DiveGramAppearance::Id(),
	.parentId = DiveGramMain::Id(),
	.title = QString("Appearance"),
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(QString("Customize the look and feel of DiveGram.")),
				st::boxLabel),
			.align = style::al_top,
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [](const SearchContext &) {
		}, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;

			AddSubsectionTitle(container, rpl::single(QString("Interface")));

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Streamer mode")),
				QString("Blur contacts, chat names and notification text "
					"while you share your screen or record a stream."),
				&AyuSettings::streamerMode,
				&AyuSettings::setStreamerMode);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Material switches")),
				QString("Replace the classic Telegram checkmarks with "
					"smooth Material-style animated switches."),
				&AyuSettings::materialSwitches,
				&AyuSettings::setMaterialSwitches);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Remove message tail")),
				QString("Hide the little tail at the edge of message "
					"bubbles for a cleaner modern look."),
				&AyuSettings::removeMessageTail,
				&AyuSettings::setRemoveMessageTail);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Simplify quotes and replies")),
				QString("Show quotes and replies as compact single-line "
					"blocks instead of full mini messages."),
				&AyuSettings::simpleQuotesAndReplies,
				&AyuSettings::setSimpleQuotesAndReplies);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Replace message info with icons")),
				QString("Show the message time, read state and edited "
					"mark as compact icons in the bubble corner."),
				&AyuSettings::replaceBottomInfoWithIcons,
				&AyuSettings::setReplaceBottomInfoWithIcons);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Unlimited recent stickers")),
				QString("Do not cap the number of recently used stickers "
					"shown at the top of the sticker panel."),
				&AyuSettings::unlimitedRecentStickers,
				&AyuSettings::setUnlimitedRecentStickers);

			AddDivider(container);
			AddSkip(container);

			AddSubsectionTitle(container, rpl::single(QString("Content")));

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Filter zalgo text")),
				QString("Strip combining characters that turn normal text "
					"into unreadable zalgo spam."),
				&AyuSettings::filterZalgo,
				&AyuSettings::setFilterZalgo);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Disable custom backgrounds")),
				QString("Ignore wallpapers set by chat owners and always "
					"use your own background in every chat."),
				&AyuSettings::disableCustomBackgrounds,
				&AyuSettings::setDisableCustomBackgrounds);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Disable auto-download of ads")),
				QString("Prevent sponsored messages from automatically "
					"downloading their media in the background."),
				&AyuSettings::disableAds,
				&AyuSettings::setDisableAds);
		});
	});
});

} // namespace

rpl::producer<QString> DiveGramAppearance::title() {
	return rpl::single(QString("Appearance"));
}

DiveGramAppearance::DiveGramAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void DiveGramAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kAppearanceMeta.build);
	Ui::ResizeFitChild(this, content);
}

namespace {

const auto kPrivacyMeta = BuildHelper({
	.id = DiveGramPrivacy::Id(),
	.parentId = DiveGramMain::Id(),
	.title = QString("Privacy & Security"),
	.icon = &st::menuIconLock,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(QString("Privacy and security options for DiveGram.")),
				st::boxLabel),
			.align = style::al_top,
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [](const SearchContext &) {
		}, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;

			AddSubsectionTitle(container, rpl::single(QString("Privacy")));

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Hide unread counters")),
				QString("Do not display the number of unread messages in "
					"the chats list or in folder badges."),
				&AyuSettings::hideNotificationCounters,
				&AyuSettings::setHideNotificationCounters);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Hide notification badges")),
				QString("Remove unread badges from the taskbar icon and "
					"the tray, so nobody sees your activity."),
				&AyuSettings::hideNotificationBadge,
				&AyuSettings::setHideNotificationBadge);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Hide stories")),
				QString("Hide the stories row from the chats list "
					"entirely."),
				&AyuSettings::disableStories,
				&AyuSettings::setDisableStories);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Hide premium statuses")),
				QString("Do not show the premium star next to users who "
					"have Telegram Premium."),
				&AyuSettings::hidePremiumStatuses,
				&AyuSettings::setHidePremiumStatuses);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Hide chats from blocked users")),
				QString("Chats of users you blocked are removed from the "
					"list until you unblock them."),
				&AyuSettings::hideFromBlocked,
				&AyuSettings::setHideFromBlocked);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Hide \u201cAll Chats\u201d folder")),
				QString("Remove the special All Chats folder from the "
					"folders bar at the top of the list."),
				&AyuSettings::hideAllChatsFolder,
				&AyuSettings::setHideAllChatsFolder);

			AddDivider(container);
			AddSkip(container);

			AddSubsectionTitle(container, rpl::single(QString("System")));

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Enable advanced filtering")),
				QString("Activate DiveGram's custom chat filters defined "
					"in the local settings file."),
				&AyuSettings::filtersEnabled,
				&AyuSettings::setFiltersEnabled);

			AddSettingToggleWithDescription(
				container,
				rpl::single(QString("Report crashes to DiveGram")),
				QString("Send anonymized crash reports to help find and "
					"fix bugs faster."),
				&AyuSettings::crashReporting,
				&AyuSettings::setCrashReporting);
		});
	});
});

} // namespace

rpl::producer<QString> DiveGramPrivacy::title() {
	return rpl::single(QString("Privacy & Security"));
}

DiveGramPrivacy::DiveGramPrivacy(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void DiveGramPrivacy::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kPrivacyMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
