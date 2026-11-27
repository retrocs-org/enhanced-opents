/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <string>

struct MapEditorPlaytest
{
	std::string Data;
	bool Scripted = false;
	int Slot = 0;
	int Opponents = 1;
};

MapEditorPlaytest UI_Map_Editor_Dialog();
bool UI_Map_Editor_Resume_Requested();

void UI_Map_Editor_Log_Trigger(char const * id, char const * description, int frame, int action = -1);
