/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#ifndef _WIN32
#include "wincompat.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_mouse.h>

#include <cstdio>


SHORT GetKeyState(int virtualkey)
{
	bool const * keys = SDL_GetKeyboardState(nullptr);
	SDL_MouseButtonFlags const buttons = SDL_GetMouseState(nullptr, nullptr);
	SDL_Keymod const modifiers = SDL_GetModState();
	bool held = false;

	switch (virtualkey) {
		case VK_LBUTTON:	held = (buttons & SDL_BUTTON_LMASK) != 0; break;
		case VK_RBUTTON:	held = (buttons & SDL_BUTTON_RMASK) != 0; break;
		case VK_MBUTTON:	held = (buttons & SDL_BUTTON_MMASK) != 0; break;
		case VK_XBUTTON1:	held = (buttons & SDL_BUTTON_X1MASK) != 0; break;
		case VK_XBUTTON2:	held = (buttons & SDL_BUTTON_X2MASK) != 0; break;
		case VK_SHIFT:		held = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]; break;
		case VK_LSHIFT:		held = keys[SDL_SCANCODE_LSHIFT]; break;
		case VK_RSHIFT:		held = keys[SDL_SCANCODE_RSHIFT]; break;
		case VK_CONTROL:	held = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]; break;
		case VK_LCONTROL:	held = keys[SDL_SCANCODE_LCTRL]; break;
		case VK_RCONTROL:	held = keys[SDL_SCANCODE_RCTRL]; break;
		case VK_MENU:		held = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]; break;
		case VK_LMENU:		held = keys[SDL_SCANCODE_LALT]; break;
		case VK_RMENU:		held = keys[SDL_SCANCODE_RALT]; break;
		case VK_CAPITAL:	return (SHORT)((keys[SDL_SCANCODE_CAPSLOCK] ? 0x8000 : 0) | ((modifiers & SDL_KMOD_CAPS) ? 1 : 0));
		case VK_NUMLOCK:	return (SHORT)((keys[SDL_SCANCODE_NUMLOCKCLEAR] ? 0x8000 : 0) | ((modifiers & SDL_KMOD_NUM) ? 1 : 0));
		case VK_SCROLL:		return (SHORT)((keys[SDL_SCANCODE_SCROLLLOCK] ? 0x8000 : 0) | ((modifiers & SDL_KMOD_SCROLL) ? 1 : 0));
		default:			break;
	}
	return (SHORT)(held ? 0x8000 : 0);
}


int MessageBoxA(HWND, char const * text, char const * caption, UINT type)
{
	SDL_MessageBoxButtonData buttons[3];
	int count = 0;
	int const style = type & 0xF;

	auto add = [&](int id, char const * label, Uint32 flags) {
		buttons[count].flags = flags;
		buttons[count].buttonID = id;
		buttons[count].text = label;
		count++;
	};

	switch (style) {
		case MB_OKCANCEL:
			add(IDOK, "OK", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
			add(IDCANCEL, "Cancel", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
			break;
		case MB_ABORTRETRYIGNORE:
			add(IDABORT, "Abort", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
			add(IDRETRY, "Retry", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
			add(IDIGNORE, "Ignore", 0);
			break;
		case MB_YESNOCANCEL:
			add(IDYES, "Yes", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
			add(IDNO, "No", 0);
			add(IDCANCEL, "Cancel", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
			break;
		case MB_YESNO:
			add(IDYES, "Yes", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
			add(IDNO, "No", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
			break;
		case MB_RETRYCANCEL:
			add(IDRETRY, "Retry", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT);
			add(IDCANCEL, "Cancel", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
			break;
		default:
			add(IDOK, "OK", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT);
			break;
	}

	Uint32 flags = SDL_MESSAGEBOX_INFORMATION;
	if ((type & 0xF0) == MB_ICONERROR) flags = SDL_MESSAGEBOX_ERROR;
	else if ((type & 0xF0) == MB_ICONWARNING) flags = SDL_MESSAGEBOX_WARNING;

	SDL_MessageBoxData data = {};
	data.flags = flags;
	data.title = caption != nullptr ? caption : "OpenTS";
	data.message = text != nullptr ? text : "";
	data.numbuttons = count;
	data.buttons = buttons;

	int pressed = buttons[0].buttonID;
	if (!SDL_ShowMessageBox(&data, &pressed)) {
		fprintf(stderr, "%s: %s\n", data.title, data.message);
		return buttons[0].buttonID;
	}
	return pressed;
}
#endif
