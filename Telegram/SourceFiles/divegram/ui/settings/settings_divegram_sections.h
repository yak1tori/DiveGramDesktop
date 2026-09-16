#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class DiveGramGeneral : public Section<DiveGramGeneral> {
public:
	DiveGramGeneral(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
private:
	void setupContent();
};

class DiveGramAppearance : public Section<DiveGramAppearance> {
public:
	DiveGramAppearance(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
private:
	void setupContent();
};

class DiveGramPrivacy : public Section<DiveGramPrivacy> {
public:
	DiveGramPrivacy(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
private:
	void setupContent();
};

} // namespace Settings