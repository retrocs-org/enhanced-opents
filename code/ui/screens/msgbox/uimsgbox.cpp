/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "ui/screens/msgbox/uimsgbox.h"

#include "ui/rml/rmlview.h"
#include "ui/uienginehost.h"

#include <sstream>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <utility>


UIMessageBoxPresenterClass::UIMessageBoxPresenterClass(std::string text, std::vector<std::string> captions, int defaultresponse) :
	Text(std::move(text)),
	Default(defaultresponse)
{
	static int const _slots[3][3] = {
		{ 1, 0, 0 },
		{ 0, 2, 0 },
		{ 0, 2, 1 },
	};

	std::vector<UIMessageButton> present;
	for (int index = 0; index < (int)captions.size() && index < 3; index++) {
		if (!captions[index].empty()) {
			UIMessageButton button;
			button.Caption = captions[index];
			button.Index = index;
			present.push_back(button);
		}
	}

	int count = (int)present.size();
	for (UIMessageButton & button : present) {
		button.Slot = _slots[count - 1][button.Index];
	}

	for (int slot = 0; slot < 3; slot++) {
		for (UIMessageButton const & button : present) {
			if (button.Slot == slot) {
				Buttons.push_back(button);
			}
		}
	}
}


void UIMessageBoxPresenterClass::Execute(UIIntent const & intent)
{
	if (intent.Name == "button") {
		Choice = intent.Value;
		Result = UI_RESULT_ACCEPTED;
	} else if (intent.Name == "ok") {
		Choice = Default;
		Result = UI_RESULT_ACCEPTED;
	} else if (intent.Name == "cancel") {
		Choice = 1;
		Result = UI_RESULT_CANCELLED;
	}
}


void UIMessageBoxPresenterClass::Refresh(void)
{
}


namespace
{

class UIMessageBoxViewClass : public UIRmlViewClass
{
	public:
		explicit UIMessageBoxViewClass(UIMessageBoxPresenterClass & presenter) :
			UIRmlViewClass(presenter, "message.rml", "message"),
			Data(presenter)
		{
		}

		virtual void Sync(void) override
		{
		}

	protected:
		virtual bool Bind(Rml::DataModelConstructor & model) override
		{
			Rml::StructHandle<UIMessageButton> button = model.RegisterStruct<UIMessageButton>();
			if (!button) {
				return(false);
			}
			button.RegisterMember("caption", &UIMessageButton::Caption);
			button.RegisterMember("index", &UIMessageButton::Index);
			button.RegisterMember("slot", &UIMessageButton::Slot);

			return(model.RegisterArray<std::vector<UIMessageButton>>() && model.Bind("text", &Data.Text) && model.Bind("buttons", &Data.Buttons));
		}

		virtual void Loaded(void) override
		{
			Rml::Element * buttons = Document()->GetElementById("buttons");
			if (buttons != nullptr) {
				buttons->SetClass("alone", Data.Buttons.size() == 1);
			}
			Document()->SetClass("net", Data.Network);
		}

	private:
		UIMessageBoxPresenterClass & Data;
};

}


std::unique_ptr<UIViewClass> UI_Message_Box_View(UIMessageBoxPresenterClass & presenter)
{
	return(std::make_unique<UIMessageBoxViewClass>(presenter));
}

namespace
{
struct FrontierTerritory
{
	std::string Name, Owner, State;
};

class FrontierPresenter final : public UIPresenterClass
{
	public:
		explicit FrontierPresenter(std::string const & briefing)
		{
			if (!briefing.starts_with("FRONTIER")) {
				Status = briefing;
				return;
			}
			std::istringstream lines(briefing);
			std::string line;
			std::getline(lines, line);
			bool history = false;
			while (std::getline(lines, line)) {
				if (line.empty()) continue;
				if (line == "RECENT BATTLES") {
					history = true;
				} else if (history) {
					if (History.size() < 3) History.push_back(line);
				} else {
					std::string const separator = " · ";
					auto const first = line.find(separator);
					if (first == std::string::npos || Territories.size() >= 3) continue;
					auto const owner = first + separator.size();
					auto const second = line.find(separator, owner);
					Territories.push_back({line.substr(0, first), line.substr(owner, second - owner),
						second == std::string::npos ? "" : line.substr(second + separator.size())});
				}
			}
			if (Territories.empty()) Status = "No Frontier territory data is available.";
		}

		void Execute(UIIntent const & intent) override
		{
			if (intent.Name == "cancel" || intent.Name == "ok") Result = UI_RESULT_CANCELLED;
		}

		void Refresh() override {}

		std::string Status;
		std::vector<FrontierTerritory> Territories;
		std::vector<std::string> History;
};

class FrontierView final : public UIRmlViewClass
{
	public:
		explicit FrontierView(FrontierPresenter & presenter) :
			UIRmlViewClass(presenter, "frontier.rml", "frontier"), Data(presenter)
		{
		}

		void Sync() override {}

	protected:
		bool Bind(Rml::DataModelConstructor & model) override
		{
			auto tile = model.RegisterStruct<FrontierTerritory>();
			if (!tile) return(false);
			tile.RegisterMember("name", &FrontierTerritory::Name);
			tile.RegisterMember("owner", &FrontierTerritory::Owner);
			tile.RegisterMember("state", &FrontierTerritory::State);
			return(model.RegisterArray<std::vector<FrontierTerritory>>()
				&& model.RegisterArray<std::vector<std::string>>()
				&& model.Bind("territories", &Data.Territories)
				&& model.Bind("history", &Data.History)
				&& model.Bind("status", &Data.Status));
		}

	private:
		FrontierPresenter & Data;
};
}


void UI_Frontier_Dialog(std::string const & briefing)
{
	FrontierPresenter presenter(briefing);
	FrontierView view(presenter);
	UI_Run_Modal(view);
}
