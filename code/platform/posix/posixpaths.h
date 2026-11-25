/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Where the POSIX port finds itself and its files. Every directory returned ends in '/'.

#pragma once

#include <string>

// The running executable, resolved through symbolic links.
std::string Posix_Executable_Path(void);

// The directory holding the executable. Inside a macOS bundle this is Contents/MacOS.
std::string Posix_Executable_Directory(void);

// The macOS bundle's Contents/Resources directory, or the executable's directory elsewhere.
std::string Posix_Resource_Directory(void);

// The directory of UI documents: Resources/ui in a bundle, ui beside the executable otherwise.
std::string Posix_UI_Directory(void);

// The directory holding a macOS bundle, so data placed beside OpenTS.app is found. Empty when
// the executable is not in a bundle.
std::string Posix_Bundle_Parent_Directory(void);

// The per-user directory for settings, saved games and screenshots:
// ~/Library/Application Support/OpenTS on macOS, $XDG_DATA_HOME/OpenTS or ~/.local/share/OpenTS
// elsewhere. Created on first use.
std::string Posix_User_Directory(void);
