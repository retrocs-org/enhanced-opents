// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors

#pragma once

#include <string>

std::string Mac_Stored_Data_Directory();
void Mac_Store_Data_Directory(std::string const & path);
std::string Mac_Choose_Data_Directory(std::string const & default_directory);
