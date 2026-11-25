/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The language strings for the POSIX port. Windows loads them from the string table of
// Language.dll; here tools/rcstrings.py compiles the same table from language.rc into the
// game, and LoadLibrary("Language.dll") returns a token that LoadString accepts.

#include "wincompat.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

struct LanguageString
{
	unsigned Id;
	char const * Text;
};

LanguageString const Strings[] = {
#include "opents_langtable.inc"
};

char LanguageToken;

}


HMODULE LoadLibraryA(char const * name)
{
	if (name != nullptr && strcasecmp(name, "Language.dll") == 0) {
		return &LanguageToken;
	}
	return nullptr;
}


BOOL FreeLibrary(HMODULE)
{
	return TRUE;
}


FARPROC GetProcAddress(HMODULE, char const *)
{
	return nullptr;
}


// The table is generated in identifier order.
int LoadStringA(HINSTANCE, UINT id, char * buffer, int size)
{
	if (buffer == nullptr || size <= 0) {
		return 0;
	}

	auto found = std::lower_bound(std::begin(Strings), std::end(Strings), id,
		[](LanguageString const & entry, unsigned key) { return entry.Id < key; });

	if (found == std::end(Strings) || found->Id != id) {
		buffer[0] = '\0';
		return 0;
	}

	size_t const length = std::min(strlen(found->Text), (size_t)size - 1);
	memcpy(buffer, found->Text, length);
	buffer[length] = '\0';
	return (int)length;
}
