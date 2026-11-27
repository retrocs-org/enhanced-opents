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

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "always.h"

#include "file.h"

#include <cstring>
#include <dirent.h>
#include <fnmatch.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

class Find_File_Data_Posix : public Find_File_Data
{
	public:
		Find_File_Data_Posix();
		virtual ~Find_File_Data_Posix();

		virtual const char * GetName() const;
		virtual const char * GetFullName() const { return DirEntry != nullptr ? FullName.c_str() : nullptr; }
		virtual unsigned int GetTime() const;

		virtual bool FindFirst(const char * fname);
		virtual bool FindNext();
		virtual void Close();

	private:
		DIR * Directory;
		struct dirent * DirEntry;
		std::string FileFilter;
		std::string FullName;
		std::string DirName;

		bool FindNextWithFilter();
};

Find_File_Data_Posix::Find_File_Data_Posix() : Directory(nullptr), DirEntry(nullptr) {}

Find_File_Data_Posix::~Find_File_Data_Posix()
{
	Close();
}

const char * Find_File_Data_Posix::GetName() const
{
	if (DirEntry == nullptr) {
		return nullptr;
	}
	return DirEntry->d_name;
}

unsigned int Find_File_Data_Posix::GetTime() const
{
	if (DirEntry == nullptr) {
		return 0;
	}
	struct stat buf = {0};
	if (stat(FullName.c_str(), &buf) != 0) {
		return false;
	}
	return buf.st_mtime;
}

bool Find_File_Data_Posix::FindNextWithFilter()
{
	while (true) {
		DirEntry = readdir(Directory);
		if (DirEntry == nullptr) {
			return false;
		}
		if (fnmatch(FileFilter.c_str(), DirEntry->d_name, FNM_PATHNAME | FNM_CASEFOLD) == 0) {
			FullName = DirName + DirEntry->d_name;
			break;
		}
	}
	return true;
}

bool Find_File_Data_Posix::FindFirst(const char * fname)
{
	Close();
	FullName.clear();
	DirName.clear();

	char const * fdir = strrchr(fname, '/');
	if (fdir != nullptr) {
		DirName.assign(fname, fdir - fname + 1);
		FileFilter = fdir + 1;
		Directory = opendir(DirName.c_str());
	} else {
		FileFilter = fname;
		Directory = opendir(".");
	}

	if (Directory == nullptr) {
		return false;
	}

	return FindNextWithFilter();
}

bool Find_File_Data_Posix::FindNext()
{
	if (Directory == nullptr) {
		return false;
	}
	return FindNextWithFilter();
}

void Find_File_Data_Posix::Close()
{
	DirEntry = nullptr;
	if (Directory != nullptr) {
		closedir(Directory);
		Directory = nullptr;
	}
}

Find_File_Data * Find_File_Data::CreateFindData()
{
	return new Find_File_Data_Posix();
}
#endif
