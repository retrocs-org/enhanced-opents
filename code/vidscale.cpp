/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "vidscale.h"

#include "_rect.h"
#include "_surface.h"
#include "globals.h"
#include "goptions.h"
#include "sidebar.h"
#include "surface.h"
#include "video.h"

#include <algorithm>
#include <cmath>


static bool Sidebar_Scaling_Active()
{
	return(GameActive && ScenarioActive && !Debug_Map && SidebarSurface != nullptr
		&& (Video_Sidebar_Width() != SidebarClass::SIDE_WIDTH || Video_Sidebar_Height() != Video_Get_Scale_Info().GameHeight));
}


static void Sidebar_Point_To_Game(Point2D & point)
{
	int const width = Video_Sidebar_Width();
	int const height = Video_Get_Scale_Info().GameHeight;
	int const start = Options.IsSidebarOnRight ? TacticalRect.Width : 0;
	if (point.X >= start && point.X < start + width && point.Y >= 0 && point.Y < height) {
		point.X = start + (point.X - start) * SidebarClass::SIDE_WIDTH / width;
		point.Y = point.Y * Video_Sidebar_Height() / height;
	} else if (!Options.IsSidebarOnRight && point.X >= width) {
		point.X += SidebarClass::SIDE_WIDTH - width;
	}
}


static void Sidebar_Point_To_Frame(Point2D & point)
{
	int const width = Video_Sidebar_Width();
	int const start = Options.IsSidebarOnRight ? TacticalRect.Width : 0;
	if (point.X >= start && point.X < start + SidebarClass::SIDE_WIDTH
		&& point.Y >= 0 && point.Y < Video_Sidebar_Height()) {
		point.X = start + (point.X - start) * width / SidebarClass::SIDE_WIDTH;
		point.Y = point.Y * Video_Get_Scale_Info().GameHeight / Video_Sidebar_Height();
	} else if (!Options.IsSidebarOnRight && point.X >= SidebarClass::SIDE_WIDTH) {
		point.X += width - SidebarClass::SIDE_WIDTH;
	}
}


/// <summary>
/// Is the frame drawn at some size or position other than the window's own?
/// </summary>
/// <returns>bool; Do window positions need converting before the game sees them?</returns>
bool Video_Scaling_Active(void)
{
	VideoScaleInfo const & scale = Video_Get_Scale_Info();

	return(scale.SourceX != 0 || scale.SourceY != 0 || scale.DestX != 0 || scale.DestY != 0 || scale.DestWidth != scale.GameWidth || scale.DestHeight != scale.GameHeight || Sidebar_Scaling_Active());
}


/// <summary>
/// Converts a position in the window's client area into one in the frame.
/// A position on one of the letterbox bars lands outside the frame rather than being
/// pulled onto its edge.
/// </summary>
/// <param name="point">The position to convert in place.</param>
void Window_Point_To_Game(Point2D & point)
{
	VideoScaleInfo const & scale = Video_Get_Scale_Info();

	if (scale.DestWidth > 0 && scale.DestHeight > 0) {
		point.X = scale.SourceX + (int)floor((point.X - scale.DestX) * (double)scale.GameWidth / (double)scale.DestWidth);
		point.Y = scale.SourceY + (int)floor((point.Y - scale.DestY) * (double)scale.GameHeight / (double)scale.DestHeight);
		if (Sidebar_Scaling_Active()) Sidebar_Point_To_Game(point);
	}
}


/// <summary>
/// Converts a position in the frame into one in the window's client area.
/// </summary>
/// <param name="point">The position to convert in place. It comes back at the top left
/// corner of the area the frame pixel covers on screen.</param>
void Game_Point_To_Window(Point2D & point)
{
	VideoScaleInfo const & scale = Video_Get_Scale_Info();

	if (scale.GameWidth > 0 && scale.GameHeight > 0) {
		if (Sidebar_Scaling_Active()) Sidebar_Point_To_Frame(point);
		point.X = scale.DestX + (int)floor((point.X - scale.SourceX) * (double)scale.DestWidth / (double)scale.GameWidth);
		point.Y = scale.DestY + (int)floor((point.Y - scale.SourceY) * (double)scale.DestHeight / (double)scale.GameHeight);
	}
}


/// <summary>
/// Pulls a position onto the frame if it lies outside it.
/// </summary>
/// <param name="point">The position to clamp in place.</param>
void Clamp_To_Game(Point2D & point)
{
	VideoScaleInfo const & scale = Video_Get_Scale_Info();

	if (Sidebar_Scaling_Active()) {
		int const width = TacticalRect.Width + SidebarClass::SIDE_WIDTH;
		point.X = std::clamp(point.X, 0, width - 1);
		bool const sidebar = Options.IsSidebarOnRight ? point.X >= TacticalRect.Width : point.X < SidebarClass::SIDE_WIDTH;
		int const height = sidebar ? Video_Sidebar_Height() : scale.GameHeight;
		point.Y = std::clamp(point.Y, 0, height - 1);
		return;
	}
	if (scale.GameWidth > 0) point.X = std::clamp(point.X, scale.SourceX, scale.SourceX + scale.GameWidth - 1);
	if (scale.GameHeight > 0) point.Y = std::clamp(point.Y, scale.SourceY, scale.SourceY + scale.GameHeight - 1);
}
