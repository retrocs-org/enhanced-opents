/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the order the mounted mixfiles are searched in, with plain archives the harness
// writes itself into a directory of its own.

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "base64.h"
#include "ccfile.h"
#include "crc.h"
#include "mixfile.h"
#include "random.h"
#include "rawfile.h"
#include "rndstraw.h"

#include "doctest/doctest.h"

// Only an encrypted mixfile draws on the random numbers, and this harness writes none.
Random3Class::Random3Class(unsigned, unsigned) : Seed(0), Index(0) {}
int Random3Class::operator() (void) { return(0); }
RandomStraw CryptRandom;

// Caching a mixfile calls back into the game loop, which this harness does not run.
void Call_Back(void)
{
}

namespace {

struct Member
{
	char const * Name;
	std::string Contents;
};


// Writes an unencrypted mixfile in the plain format: the member count and data size, the
// directory sorted by CRC, then the members' data.
void Write_Mixfile(char const * filename, std::vector<Member> const & members)
{
	std::vector<MixFileClass::SubBlock> blocks;
	std::string data;

	for (Member const & member : members) {
		std::string name = member.Name;
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return((char)std::toupper(c)); });

		MixFileClass::SubBlock block;
		block.CRC = CRCEngine()(name.c_str(), (int)name.size());
		block.Offset = (int)data.size();
		block.Size = (int)member.Contents.size();
		blocks.push_back(block);
		data += member.Contents;
	}
	std::sort(blocks.begin(), blocks.end(), [](MixFileClass::SubBlock const & one, MixFileClass::SubBlock const & two) { return(one.CRC < two.CRC); });

	short const count = (short)blocks.size();
	int const size = (int)data.size();

	RawFileClass file(filename);
	file.Open(FileClass::WRITE);
	file.Write(&count, sizeof(count));
	file.Write(&size, sizeof(size));
	file.Write(blocks.data(), (int)(blocks.size() * sizeof(MixFileClass::SubBlock)));
	file.Write(data.data(), (int)data.size());
	file.Close();
}


std::string Found_In(char const * filename)
{
	MixFileClass * mixfile = NULL;
	if (!MixFileClass::Offset(filename, NULL, &mixfile) || mixfile == NULL) {
		return(std::string());
	}
	std::string name = std::filesystem::path(mixfile->Filename).filename().string();
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return((char)std::toupper(c)); });
	return(name);
}


struct MixDirectoryFixture
{
	std::filesystem::path OriginalDirectory;
	std::filesystem::path Root;

	MixDirectoryFixture() : OriginalDirectory(std::filesystem::current_path())
	{
		Root = std::filesystem::temp_directory_path() / ("opents-mixorder-" + std::to_string(GetCurrentProcessId()));
		REQUIRE(std::filesystem::create_directory(Root));
		std::filesystem::current_path(Root);
	}

	~MixDirectoryFixture()
	{
		CDFileClass::Clear_Search_Drives();
		CDFileClass::Set_Data_Path("");
		std::error_code error;
		std::filesystem::current_path(OriginalDirectory, error);
		std::filesystem::remove_all(Root, error);
	}
};

}


TEST_CASE_FIXTURE(MixDirectoryFixture, "a mixfile moved in the search order supplies a shared name from its new place")
{
	Write_Mixfile("FIRST.MIX", { { "SHARED.TXT", "first" }, { "ONLYFIRST.TXT", "first" } });
	Write_Mixfile("SECOND.MIX", { { "SHARED.TXT", "second" } });
	Write_Mixfile("THIRD.MIX", { { "SHARED.TXT", "third" } });

	MixFileClass * first = new MixFileClass("FIRST.MIX", NULL);
	MixFileClass * second = new MixFileClass("SECOND.MIX", NULL);
	MixFileClass * third = new MixFileClass("THIRD.MIX", NULL);
	REQUIRE_MESSAGE((first->Is_Valid() && second->Is_Valid() && third->Is_Valid()), "the harness's archives open");

	CHECK_MESSAGE(Found_In("SHARED.TXT") == "FIRST.MIX", "the archive mounted first supplies a shared name");

	third->Search_After(first);
	CHECK_MESSAGE(Found_In("SHARED.TXT") == "FIRST.MIX", "an archive moved behind the first leaves it in front");
	second->Search_Last();
	CHECK_MESSAGE((first->Next() == third && third->Next() == second), "an archive moved behind another is searched right after it");

	third->Search_After(NULL);
	CHECK_MESSAGE(Found_In("SHARED.TXT") == "THIRD.MIX", "an archive moved to the front supplies a shared name");
	CHECK_MESSAGE(Found_In("ONLYFIRST.TXT") == "FIRST.MIX", "a name only a later archive holds is still found");

	third->Search_Last();
	first->Search_Last();
	CHECK_MESSAGE(Found_In("SHARED.TXT") == "SECOND.MIX", "archives moved to the back are searched after the rest");
	first->Search_Last();
	CHECK_MESSAGE((second->Next() == third && third->Next() == first && first->Next_Valid() == NULL), "moving the last archive to the back keeps the list whole");

	MixFileClass * missing = new MixFileClass("MISSING.MIX", NULL);
	CHECK_MESSAGE(!missing->Is_Valid(), "an archive that is not there is not searched");
	missing->Search_After(NULL);
	missing->Search_Last();
	CHECK_MESSAGE(!missing->Is_Valid(), "moving an archive that failed to open leaves it out of the search");
	CHECK_MESSAGE(Found_In("SHARED.TXT") == "SECOND.MIX", "an archive that failed to open does not disturb the search");

	delete missing;
	delete first;
	delete second;
	delete third;
	CHECK_MESSAGE(Found_In("SHARED.TXT").empty(), "a deleted archive leaves the search");
}


TEST_CASE_FIXTURE(MixDirectoryFixture, "nested archives use member names with a separate data directory")
{
	Write_Mixfile("inner.mix", { { "marker.txt", "nested" } });
	std::string image;
	{
		std::ifstream stream("inner.mix", std::ios::binary);
		image.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}
	REQUIRE(!image.empty());
	REQUIRE(std::filesystem::remove("inner.mix"));

	std::filesystem::create_directory("data");
	Write_Mixfile("data/outer.mix", { { "inner.mix", image } });
	std::string const data = (Root / "data").string() + "/";
	CDFileClass::Set_Data_Path(data.c_str());
	CDFileClass::Add_Search_Drive(data.c_str());

	MixFileClass outer("OUTER.MIX", nullptr);
	REQUIRE(outer.Is_Valid());
	MixFileClass inner("INNER.MIX", nullptr);
	REQUIRE(inner.Is_Valid());
	REQUIRE(MixFileClass::Cache("INNER.MIX"));
	CCFileClass member("MARKER.TXT");
	REQUIRE(member.Open());
	char contents[6];
	REQUIRE(member.Read(contents, sizeof(contents)) == sizeof(contents));
	CHECK(std::string(contents, sizeof(contents)) == "nested");
}


TEST_CASE("Base64 uses the standard bit order regardless of platform")
{
	unsigned char const expected[] = { 0x02, 0x28, 0x51, 0xBC };
	unsigned char decoded[8] = {};
	char encoded[16] = {};
	REQUIRE(Base64_Decode("AihR vA==", 9, decoded, sizeof(decoded)) == sizeof(expected));
	CHECK(std::memcmp(decoded, expected, sizeof(expected)) == 0);
	REQUIRE(Base64_Encode(expected, sizeof(expected), encoded, sizeof(encoded)) == 8);
	CHECK(std::string(encoded) == "AihRvA==");
}


TEST_CASE_FIXTURE(MixDirectoryFixture, "a short MIX header stays out of the search")
{
	std::ofstream("short.mix", std::ios::binary).write("\x01", 1);
	MixFileClass archive("short.mix", nullptr);
	CHECK_FALSE(archive.Is_Valid());
}
