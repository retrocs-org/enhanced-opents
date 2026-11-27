// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <doctest/doctest.h>

#include "cdfile.h"

namespace {

class SearchFixture
{
	public:
		SearchFixture() : WorkingDirectory(std::filesystem::current_path())
		{
			std::string name = (std::filesystem::temp_directory_path() / "opents-data-XXXXXX").string();
			REQUIRE(mkdtemp(name.data()) != nullptr);
			Root = name;
		}

		~SearchFixture()
		{
			CDFileClass::Clear_Search_Drives();
			CDFileClass::Set_Data_Path("");
			CDFileClass::Set_User_Path("");
			std::error_code error;
			std::filesystem::current_path(WorkingDirectory, error);
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path WorkingDirectory;
		std::filesystem::path Root;
};


char Read_Marker(char const * filename)
{
	CDFileClass file(filename);
	REQUIRE(file.Open(FileClass::READ));
	char marker = '\0';
	REQUIRE(file.Read(&marker, 1) == 1);
	return(marker);
}

}


TEST_CASE("Selected data takes precedence over the executable directory")
{
	SearchFixture fixture;
	std::filesystem::current_path(fixture.Root);
	std::filesystem::create_directories("data/extra");
	std::filesystem::create_directory("user");
	std::ofstream("rules.ini") << 'e';
	std::ofstream("data/rules.ini") << 'd';
	std::ofstream("data/extra/art.ini") << 'a';
	std::ofstream("only-executable.ini") << 'e';

	CDFileClass::Add_Search_Drive("data/");
	CDFileClass::Add_Search_Drive("data/extra/");
	CHECK(Read_Marker("rules.ini") == 'e');

	CDFileClass::Set_Data_Path("data/");
	CHECK(Read_Marker("rules.ini") == 'd');
	CHECK(Read_Marker("art.ini") == 'a');
	CHECK_FALSE(CDFileClass("only-executable.ini").Is_Available());
	CHECK(Read_Marker("./rules.ini") == 'e');

	CDFileClass::Set_User_Path("user/");
	std::ofstream("user/rules.ini") << 'u';
	CHECK(Read_Marker("rules.ini") == 'u');
}


TEST_CASE("Mixed-case game paths resolve through non-ASCII directories")
{
	SearchFixture fixture;
	std::filesystem::current_path(fixture.Root);
	std::filesystem::create_directories("Donn\xC3\xA9" "es/MiXeD");
	std::ofstream("Donn\xC3\xA9" "es/MiXeD/RuLeS.InI") << 'd';
	std::filesystem::create_directory("Joueurs \xC3\xA9");
	std::ofstream("Joueurs \xC3\xA9/options.ini") << 'u';

	CDFileClass::Add_Search_Drive("donn\xC3\xA9" "es/mixed/");
	CDFileClass::Set_Data_Path("donn\xC3\xA9" "es/mixed/");
	CHECK(Read_Marker("rules.ini") == 'd');

	CDFileClass::Set_User_Path("joueurs \xC3\xA9/");
	CHECK(Read_Marker("OPTIONS.INI") == 'u');
}


TEST_CASE("Long POSIX paths work for configured data and user overrides")
{
	SearchFixture fixture;
	std::filesystem::path const component(std::string(180, 'a'));
	std::filesystem::path const root = fixture.Root / component / component / component;
	std::filesystem::path const data = root / "data";
	std::filesystem::path const user = root / "user";
	std::filesystem::create_directories(data);
	std::filesystem::create_directories(user);
	std::ofstream(data / "rules.ini") << 'd';
	std::ofstream(user / "rules.ini") << 'u';

	std::string const search = data.string() + "/";
	CDFileClass::Set_Data_Path(search.c_str());
	CDFileClass::Add_Search_Drive(search.c_str());
	CHECK(Read_Marker("rules.ini") == 'd');
	CDFileClass::Set_User_Path((user.string() + "/").c_str());
	CHECK(Read_Marker("rules.ini") == 'u');
}
