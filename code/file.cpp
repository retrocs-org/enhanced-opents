/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2020-2024 Vanilla Conquer contributors
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Vanilla Conquer
 * (https://github.com/TheAssemblyArmada/Vanilla-Conquer).
 * Modified by OpenTS contributors, 2026.
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "file.h"

#include <cstring>

#ifdef __APPLE__
#include <cerrno>
#include <limits.h>
#include <string>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef _WIN32
static void Resolve_File_Single(char * fname)
{
#ifdef __APPLE__
	if (*fname != '\0' && strpbrk(fname, "*?[") == nullptr) {
		struct attrlist attributes = {};
		attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
		attributes.commonattr = ATTR_CMN_NAME;

		struct {
			uint32_t Size;
			attrreference_t Name;
			char Storage[NAME_MAX + 1];
		} entry;

		if (getattrlist(fname, &attributes, &entry, sizeof(entry), FSOPT_NOFOLLOW) == 0) {
			char * basename = strrchr(fname, '/');
			basename = basename != nullptr ? basename + 1 : fname;
			if (entry.Name.attr_length == strlen(basename) + 1) {
				char const * name = reinterpret_cast<char const *>(&entry.Name) + entry.Name.attr_dataoffset;
				memcpy(basename, name, entry.Name.attr_length);
			}
			return;
		}
	}
#endif
	Find_File_Data * ffblk;

	ffblk = Find_File_Data::CreateFindData();

	if (ffblk == nullptr) {
		return;
	}

	size_t name_len = strlen(fname);

	if (ffblk->FindFirst(fname) && name_len == strlen(ffblk->GetFullName())) {
		strncpy(fname, ffblk->GetFullName(), name_len + 1);
	}

	delete ffblk;
}
#endif

void Resolve_File(char * fname)
{
#ifndef _WIN32
#ifdef _PC_CASE_SENSITIVE
	if (strpbrk(fname, "*?[") == nullptr) {
		struct stat info;
		if (lstat(fname, &info) != 0 && errno == ENOENT) {
			std::string parent(fname);
			for (;;) {
				auto const slash = parent.find_last_of('/');
				if (slash == std::string::npos) {
					parent = ".";
				} else {
					parent.resize(slash == 0 ? 1 : slash);
				}

				long const case_sensitive = pathconf(parent.c_str(), _PC_CASE_SENSITIVE);
				if (case_sensitive == 0) return;
				if (case_sensitive != -1 || errno != ENOENT || parent == "/" || parent == ".") break;
			}
		}
	}
#endif
	// step through each sub-directory before going for the win
	char * next = fname;
	while (next = strchr(next, '/')) {
		*next = '\0';
		Resolve_File_Single(fname);
		*next++ = '/';
	}

	Resolve_File_Single(fname);
#endif
}

bool Find_First(const char * fname, unsigned int mode, Find_File_Data ** ffblk)
{
	if (ffblk == nullptr) {
		return false;
	}
	*ffblk = nullptr;

	*ffblk = Find_File_Data::CreateFindData();
	if ((*ffblk)->FindFirst(fname)) {
		return true;
	}

	delete *ffblk;
	*ffblk = nullptr;

	return false;
}

bool Find_Next(Find_File_Data * ffblk)
{
	if (ffblk == nullptr) {
		return false;
	}

	return ffblk->FindNext();
}

void Find_Close(Find_File_Data * ffblk)
{
	if (ffblk != nullptr) {
		delete ffblk;
	}
}
