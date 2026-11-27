/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "_map.h"
#include "_rules.h"
#include "_tooltip.h"
#include "_ui.h"
#include "audio/audioengine.h"
#include "ccini.h"
#include "data.h"
#include "gamedlg.h"
#include "globals.h"
#include "goptions.h"
#include "mainopt.h"
#include "misc.h"
#include "newmenu.h"
#include "queue.h"
#include "session.h"
#include "techno.h"
#include "ui/screens/gamectrl/uigamectrl.h"
#include "ui/uienginehost.h"
#include "ui/uishell.h"
#include "ui/uiview.h"
#include "uitooltip.h"
#include "video.h"

#include <algorithm>
#include <iterator>


namespace
{

bool Is_In_Scenario(void)
{
	return(GameActive && ScenarioActive && PlayerPtr != NULL);
}


class UIGameControlsEngineServiceClass : public UIGameControlsServiceClass
{
	public:
		// During a scenario the speed is a simulation input, so it changes through the event
		// in every session type; only in a game played alone is the pick also the setting.
		virtual void Set_Game_Speed(int speed) override
		{
			if (!Is_In_Scenario()) {
				Options.GameSpeed = speed;
				return;
			}

			if (Session.Pending_Game_Speed() == speed) return;

			if (!Session.Is_Network_Game()) {
				Options.GameSpeed = speed;
			}
			OutList.push_back(EventClass(PlayerPtr->HeapID, EventClass::GAMESPEED, speed));
		}

		virtual void Set_Scroll_Rate(int rate) override
		{
			Options.ScrollRate = rate;
		}

		virtual void Set_Detail_Level(int level) override
		{
			if (Options.DetailLevel != level) {
				Options.DetailLevel = level;
				Map.Reinit_Cell_Drawers();
			}
		}

		virtual void Set_Cameo_Text(bool on) override
		{
			if (Options.SidebarCameoText != on) {
				Options.SidebarCameoText = on;
				Map.Toggle_Cameo_Text(on);
			}
		}

		virtual void Set_Action_Lines(bool on) override
		{
			Options.ActionLines = on;
			TechnoClass::Set_Action_Lines(Options.ActionLines);
		}

		virtual void Set_Tool_Tips(bool on) override
		{
			Options.ToolTips = on;
			if (ToolTips != NULL && GameActive == true) {
				ToolTips->Activate(Options.ToolTips);
			}
		}

		virtual void Set_Scroll_Coasting(bool on) override
		{
			Options.ScrollMethod = on ? 0 : 1;
		}

		virtual void Set_Edge_Scroll(bool on) override
		{
			Options.AutoScroll = on;
		}

		virtual void Set_Difficulty(int difficulty) override
		{
			Options.Difficulty = difficulty;
		}

		virtual void Save(void) override
		{
			Options.Save_Settings();
		}
};

UIGameControlsEngineServiceClass _Service;


void Fetch_Names(std::vector<std::string> & names, int const * ids, int count)
{
	names.clear();
	for (int index = 0; index < count; index++) {
		names.push_back(Fetch_String(ids[index]));
	}
}

}


UIGameControlsServiceClass & UI_Game_Controls_Service(void)
{
	return(_Service);
}


void UI_Game_Controls_State(UIGameControlsState & state)
{
	state = UIGameControlsState();

	state.Speed = Is_In_Scenario() ? Session.Pending_Game_Speed() : Options.GameSpeed;
	state.Scroll = Options.ScrollRate;
	state.Detail = Options.DetailLevel;
#ifdef __APPLE__
	state.HasUIScale = true;
	state.MenuSize = Video_Menu_UI_Size();
	auto const first = std::begin(GameplayUIScalePresets);
	auto const last = std::end(GameplayUIScalePresets);
	state.UIScale = static_cast<int>(std::find(first, last, Video_Gameplay_UI_Scale()) - first);
	state.GraphicsScale = static_cast<int>(std::find(first, last, Video_Gameplay_Graphics_Scale()) - first);
#endif
	state.Difficulty = Options.Difficulty;
	state.CameoText = Options.SidebarCameoText;
	state.ActionLines = Options.ActionLines;
	state.ToolTips = Options.ToolTips;
	state.Coasting = (Options.ScrollMethod == 0);
	state.EdgeScroll = Options.AutoScroll;
	state.InGame = GameActive;
	state.HasDifficulty = !GameActive;
	state.SoundEnabled = AudioEngine.Is_Available();

	Fetch_Names(state.SpeedNames, GameSpeedNames, OptionsClass::MAX_SPEED_SETTING);
	Fetch_Names(state.ScrollNames, GameScrollSpeedNames, OptionsClass::MAX_SCROLL_SETTING);
	Fetch_Names(state.DetailNames, GameDetailLevelNames, OptionsClass::MAX_DETAIL_SETTING);
	Fetch_Names(state.DifficultyNames, GameDifficultyNames, OptionsClass::MAX_DIFFICULTY_SETTING);
}


void UI_Game_Controls_Dialog(void)
{
	UIGameControlsState state;
	UI_Game_Controls_State(state);

	UIGameControlsPresenterClass presenter(UI_Game_Controls_Service(), state);
	std::unique_ptr<UIViewClass> view = UI_Game_Controls_View(presenter);

	UIResult const result = UI_Run_Modal(*view);
	if (result == UI_RESULT_ACCEPTED) {
		bool changed = false;
#ifdef __APPLE__
		if (presenter.State.MenuSize != state.MenuSize) {
			ConfigINI.Put_Int("Video", "MenuUISize", presenter.State.MenuSize);
			changed = true;
		}

		if (presenter.State.UIScale != state.UIScale || presenter.State.GraphicsScale != state.GraphicsScale) {
			ConfigINI.Put_Int("Video", "UIScale", GameplayUIScalePresets[presenter.State.UIScale]);
			ConfigINI.Put_Int("Video", "GraphicsScale", GameplayUIScalePresets[presenter.State.GraphicsScale]);
			if (Change_Display_Mode(Options.ScreenWidth, Options.ScreenHeight)) {
				changed = true;
				if (!state.InGame) Draw_Menu_Background();
			} else {
				ConfigINI.Put_Int("Video", "UIScale", GameplayUIScalePresets[state.UIScale]);
				ConfigINI.Put_Int("Video", "GraphicsScale", GameplayUIScalePresets[state.GraphicsScale]);
			}
		}
#endif
		if (changed) Options.Save_Settings();
	}

	if (presenter.Next == UIGameControlsPresenterClass::NEXT_SOUND) {
		SpecialDialog = SDLG_SOUND;
	} else if (presenter.Next == UIGameControlsPresenterClass::NEXT_KEYBOARD) {
		SpecialDialog = SDLG_KEYBOARD;
	}
}
