/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "ui/screens/mapedit/uimapedit.h"

#include "_ui.h"
#include "_pk.h"
#include "cdfile.h"
#include "addon.h"
#include "ccfile.h"
#include "init.h"
#include "ini.h"
#include "mixfile.h"
#include "rect.h"
#include "theater.h"
#include "terrtype.h"
#include "globals.h"
#include "houstype.h"
#include "lzostraw.h"
#include "lzopipe.h"
#include "lcw.h"
#include "lcwpipe.h"
#include "overlay.hh"
#include <span>
#include "xpipe.h"
#include "xstraw.h"
#include "ui/rml/rmlimage.h"
#include "ui/rml/rmlsurface.h"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include "ui/rml/rmlview.h"
#include "ui/uienginehost.h"
#include "ui/uishell.h"
#include "video.h"
#include "windowevent.hh"
#include "gamedirs.h"
#include "sdl/sdlwindow.h"

#include "ui/rml/rmltexture.h"
#include <chrono>
#include <random>
#include "waypoint.h"
#include "waypoint.hh"
#include "taction.h"
#include "tevent.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <locale>
#include <sstream>
#include <utility>
#include <tuple>
#include <map>
#include <set>
#include <string_view>

namespace
{

namespace FS = std::filesystem;

std::string_view Trim(std::string_view text)
{
	auto const first = text.find_first_not_of(" \t");
	if (first == text.npos) return(std::string_view{});
	return(text.substr(first, text.find_last_not_of(" \t") - first + 1));
}


int Read_Number(std::string_view text, int base = 10)
{
	if (text.empty()) return(-1);
	int value = -1;
	auto const end = text.data() + text.size();
	auto const result = std::from_chars(text.data(), end, value, base);
	return(result.ec == std::errc{} && result.ptr == end ? value : -1);
}


int Read_Cell(std::string_view value)
{
	if (value.starts_with('$')) return(Read_Number(value.substr(1), 16));
	if (value.ends_with('h') || value.ends_with('H')) return(Read_Number(value.substr(0, value.size() - 1), 16));
	return(Read_Number(value));
}


bool Equal_Section(std::string_view text, std::string_view lower)
{
	return(std::ranges::equal(text, lower, [](char a, char b) {
		return((a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) == b);
	}));
}


struct WaypointEntry
{
	std::size_t Start;
	std::size_t Length;
	int Cell;
};


struct ScenarioDocument
{
	std::string Source;
	std::string Newline = "\r\n";
	std::size_t InsertAt = std::string::npos;
	std::array<std::optional<WaypointEntry>, WAYPT_COUNT> Waypoints;

	bool Scan()
	{
		bool has_map = false;
		bool seen_waypoints = false;
		bool in_waypoints = false;
		auto const first_break = Source.find_first_of("\r\n");
		if (first_break != std::string::npos) {
			Newline = Source.substr(first_break, Source.compare(first_break, 2, "\r\n") == 0 ? 2 : 1);
		}

		for (std::size_t start = 0; start < Source.size();) {
			auto const end = Source.find_first_of("\r\n", start);
			std::string_view line = std::string_view(Source).substr(start, end - start);
			auto const statement = line.substr(0, line.find(';'));
			auto const text = Trim(statement);
			if (text.starts_with('[') && text.ends_with(']')) {
				if (in_waypoints) InsertAt = start;
				auto const name = Trim(text.substr(1, text.size() - 2));
				has_map |= Equal_Section(name, "map");
				in_waypoints = Equal_Section(name, "waypoints");
				if (in_waypoints && seen_waypoints) return(false);
				seen_waypoints |= in_waypoints;
			} else if (in_waypoints) {
				auto const equals = statement.find('=');
				if (equals != statement.npos) {
					auto const key = Trim(statement.substr(0, equals));
					int const slot = Read_Number(key);
					if (slot >= 0 && slot < WAYPT_COUNT && key == std::to_string(slot)) {
						auto const value = Trim(statement.substr(equals + 1));
						int const cell = Read_Cell(value);
						if (Waypoints[slot] || cell < 0 || cell % 1000 > 511 || cell / 1000 > 511) return(false);
						Waypoints[slot] = WaypointEntry{start + static_cast<std::size_t>(value.data() - line.data()), value.size(), cell};
					}
				}
			}
			if (end == std::string::npos) break;
			start = end + (Source.compare(end, 2, "\r\n") == 0 ? 2 : 1);
		}
		if (in_waypoints) InsertAt = Source.size();
		return(has_map);
	}

	std::string Export(std::map<int, int> const & changes) const
	{
		std::map<std::size_t, std::pair<std::size_t, std::string>, std::greater<>> edits;
		std::string added;
		for (auto const & [slot, cell] : changes) {
			if (auto const & original = Waypoints[slot]) {
				edits.emplace(original->Start, std::pair(original->Length, std::to_string(cell)));
			} else {
				added += std::to_string(slot) + '=' + std::to_string(cell) + Newline;
			}
		}
		if (!added.empty()) {
			auto const offset = InsertAt == std::string::npos ? Source.size() : InsertAt;
			if (InsertAt == std::string::npos) added.insert(0, "[Waypoints]" + Newline);
			if (offset != 0 && Source[offset - 1] != '\n' && Source[offset - 1] != '\r') added.insert(0, Newline);
			edits.emplace(offset, std::pair(0, std::move(added)));
		}
		std::string result = Source;
		for (auto const & [offset, edit] : edits) result.replace(offset, edit.first, edit.second);
		return(result);
	}
};


ScenarioDocument Create_Scenario(int width, int height, std::string const & name, bool snow, bool sample)
{
	ScenarioDocument document;
	document.Source = std::string("[Basic]\r\nName=") + name
		+ "\r\nNewINIFormat=4\r\nMultiplayerOnly=1\r\n\r\n[Map]\r\nTheater="
		+ (snow ? "SNOW" : "TEMPERATE") + "\r\nSize=0,0,"
		+ std::to_string(width) + ',' + std::to_string(height)
		+ "\r\nLocalSize=2,4," + std::to_string(width - 4) + ',' + std::to_string(height - 10)
		+ "\r\n\r\n[Waypoints]\r\n";
	if (sample) {
		int const first_start = 48;
		int const second_start = 80;
		document.Source += "0=" + std::to_string(first_start * 1001) + "\r\n1="
			+ std::to_string(second_start * 1001) + "\r\n\r\n[Terrain]\r\n";
		for (int center : {first_start + 9, second_start - 9}) {
			for (int side : {-1, 1}) {
				document.Source += std::to_string((center + side * 9) * 1000 + center - side * 9)
					+ "=TIBTRE01\r\n";
			}
		}
	}
	document.Scan();
	return(document);
}


ScenarioDocument Create_Mission_Sample()
{
	ScenarioDocument document;
	document.Source =
		"[Basic]\nName=Relay Outpost\nNewINIFormat=4\nPlayer=GDI\nOneTimeOnly=yes\n"
		"[Map]\nTheater=TEMPERATE\nSize=0,0,64,64\nLocalSize=2,4,60,54\n"
		"[Waypoints]\n0=48048\n1=54054\n98=48048\n"
		"[Houses]\n0=GDI\n1=Nod\n"
		"[GDI]\nCredits=30\nEdge=West\n"
		"[Nod]\nCredits=0\nEdge=East\n"
		"[Structures]\n0=GDI,GAPOWR,256,48,48,0,None,0,0,0,0,0,0,0,0,0,0\n"
		"1=Nod,NAPOWR,256,62,62,0,None,0,0,0,0,0,0,0,0,0,0\n"
		"[Units]\n0=GDI,SMECH,256,52,52,0,Guard,None,0,-1,0,-1\n"
		"[TaskForces]\n0=7E100010\n[7E100010]\nName=Relay response\n0=2,SMECH\nGroup=-1\n"
		"[ScriptTypes]\n0=7E100011\n[7E100011]\nName=Hold for orders\n0=5,1\n"
		"[TeamTypes]\n0=7E100012\n[7E100012]\nName=GDI response\nHouse=GDI\n"
		"Waypoint=B\nTaskForce=7E100010\nScript=7E100011\nMax=1\n"
		"[Tutorial]\n1=Destroy the Nod relay southeast of your base before the deadline.\n"
		"[Triggers]\n7E100001=GDI,<none>,Mission orders,0,1,1,1,0\n"
		"7E100002=GDI,<none>,GDI reinforcements,0,1,1,1,0\n"
		"7E100003=GDI,<none>,Relay destroyed,0,1,1,1,0\n"
		"7E100004=GDI,<none>,Time expired,0,1,1,1,0\n"
		"[Events]\n";
	int const gdi = HouseTypeClass::From_Name("GDI"), nod = HouseTypeClass::From_Name("Nod");
	for (auto const & [id, type, value] : {std::tuple{"7E100001", TEVENT_TIME, 1},
		{"7E100002", TEVENT_TIME, 5}, {"7E100003", TEVENT_ALL_DESTROYED, nod}, {"7E100004", TEVENT_TIME, 120}})
		document.Source += std::string(id) + "=1," + std::to_string(type) + ",0," + std::to_string(value) + '\n';
	document.Source += "[Actions]\n";
	auto action = [&](char const * id, int type, int code, std::string param, int seconds = 0) {
		document.Source += std::string(id) + "=1," + std::to_string(type) + ',' + std::to_string(code)
			+ ',' + param + ",-1," + std::to_string(seconds) + ",0,0,0\n";
	};
	action("7E100001", TACTION_TEXT_TRIGGER, 0, "1", 12);
	action("7E100002", TACTION_REINFORCEMENTS, 1, "7E100012");
	action("7E100003", TACTION_WIN, 0, std::to_string(gdi));
	action("7E100004", TACTION_LOSE, 0, std::to_string(gdi));
	document.Source += "[Tags]\n7E100001=0,Orders,7E100001\n7E100002=0,Reinforcements,7E100002\n"
		"7E100003=0,Victory,7E100003\n7E100004=0,Defeat,7E100004\n";
	document.Scan();
	return(document);
}

struct WaypointRow
{
	int Slot;
	int X;
	int Y;
};


constexpr std::array<std::array<int, 3>, 8> PlayerStartColors{{
	{104, 202, 255}, {255, 151, 105}, {164, 220, 114}, {215, 164, 255},
	{255, 222, 105}, {255, 155, 202}, {104, 226, 212}, {224, 224, 224},
}};

constexpr std::array<std::array<int, 5>, 8> PlayerStartDigits{{
	{2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
	{7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7},
}};

struct EditorTagChoice { std::string Id, Label; };

std::string Script_Step_Name(bool action, int type)
{
	if (action) {
		switch (type) {
			case TACTION_REINFORCEMENTS: return("Reinforce team");
			case TACTION_CREATE_TEAM: return("Create team");
			case TACTION_TEXT_TRIGGER: return("Display objective text");
			case TACTION_WIN: return("Win mission");
			case TACTION_LOSE: return("Lose mission");
			case TACTION_FORCE_TRIGGER: return("Force trigger");
			case TACTION_ION_LIGHTNING_STRIKE: return("Lightning strike");
			default: return("Action " + std::to_string(type));
		}
	}
	switch (type) {
		case TEVENT_TIME: return("Elapsed time");
		case TEVENT_ALL_DESTROYED: return("House destroyed");
		case TEVENT_ANY: return("Any object event");
		default: return("Event " + std::to_string(type));
	}
}

struct ActorFormat { int Section, Facing, Tag; char const * Types, *Default; };
constexpr ActorFormat ActorFormats[] = {
	{12, 5, 7, "VehicleTypes", "MTNK"}, {11, 5, 6, "BuildingTypes", "GAPOWR"},
	{13, 7, 8, "InfantryTypes", "E1"}, {14, 5, 7, "AircraftTypes", "ORCA"},
};

struct PlaytestIssue
{
	int Slot;
	std::string Text;
	int Section = -1;
	std::string Key;
	int Cell = -1;
	bool Blocking = false;
};

struct ScriptSection
{
	char const * Name;
	char const * Key;
};

constexpr ScriptSection ScriptSections[] = {
	{"Triggers", "triggers"}, {"Events", "events"}, {"Actions", "actions"},
	{"Tags", "tags"}, {"CellTags", "celltags"}, {"TeamTypes", "teamtypes"},
	{"TaskForces", "taskforces"}, {"ScriptTypes", "scripttypes"}, {"Tutorial", "tutorial"},
	{"Lighting", "lighting"}, {"Terrain", "terrain"}, {"Structures", "structures"},
	{"Units", "units"}, {"Infantry", "infantry"}, {"Aircraft", "aircraft"}, {"Tubes", "tubes"}, {"Map", "map"},
};


using TerrainRecord = std::array<unsigned char, 11>;

struct EditorTileState
{
	std::optional<TerrainRecord> Record;
	int Color = 0;
	bool operator==(EditorTileState const &) const = default;
};

struct EditorTileChange
{
	EditorTileState Before;
	EditorTileState After;
};

struct TileBrush
{
	char const * Name;
	char const * Key;
	std::array<int, 3> Color;
};

constexpr TileBrush Brushes[] = {
	{"Clear ground", "ClearTile", {78, 103, 63}},
	{"Rough ground", "RoughTile", {123, 93, 65}},
	{"Sand", "SandTile", {179, 146, 82}},
	{"Pavement", "PaveTile", {130, 130, 138}},
	{"Water", "WaterSet", {47, 106, 170}},
};


// Terrain and scripting edits replace only the named section in the scenario copy.
bool Replace_Section(std::string & source, std::string_view name, std::string_view replacement, std::string_view newline)
{
	std::size_t begin = source.npos;
	std::size_t end = source.size();
	bool found = false;
	for (std::size_t start = 0; start < source.size();) {
		auto const eol = source.find_first_of("\r\n", start);
		auto line = std::string_view(source).substr(start, eol - start);
		line = Trim(line.substr(0, line.find(';')));
		if (line.starts_with('[') && line.ends_with(']')) {
			if (begin != source.npos && end == source.size()) end = start;
			if (Equal_Section(Trim(line.substr(1, line.size() - 2)), name)) {
				if (found) return(false);
				begin = start;
				found = true;
			}
		}
		if (eol == source.npos) break;
		start = eol + (source.compare(eol, 2, "\r\n") == 0 ? 2 : 1);
	}
	if (!found) {
		if (!source.empty() && source.back() != '\r' && source.back() != '\n') source += newline;
		source += replacement;
	} else {
		source.replace(begin, end - begin, replacement);
	}
	return(true);
}


struct EditorViewImage
{
	int Width = 0;
	int Height = 0;
	int OriginX = 0;
	int OriginY = 0;
	int HalfTileX = 0;
	int HalfTileY = 0;
	std::vector<std::uint8_t> Pixels;
};


class EditorRenderJob
{
	public:
		~EditorRenderJob() { Stop(); }

		bool Start(std::string const & source)
		{
			Stop();
			std::error_code error;
			auto const temp = FS::temp_directory_path(error);
			if (error) return(false);
			std::random_device random;
			for (int attempt = 0; attempt < 8; ++attempt) {
				auto path = temp / ("opents-editor-" + std::to_string(random()));
				if (FS::create_directory(path, error)) {
					Directory = std::move(path);
#ifndef _WIN32
					FS::permissions(Directory, FS::perms::owner_all, error);
					if (error) {
						Stop();
						return(false);
					}
#endif
					break;
				}
			}
			if (Directory.empty()) return(false);
			auto utf8 = [](FS::path const & path) {
				auto const text = path.u8string();
				return(std::string(text.begin(), text.end()));
			};
			auto const map = Directory / "draft.mpr";
			auto const png = Directory / "map.png";
			std::ofstream stream(map, std::ios::binary);
			stream.write(source.data(), source.size());
			stream.close();
			if (!stream) return(false);
			auto const map_arg = "-CAPTUREMAP=" + utf8(map);
			auto const png_arg = "-CAPTUREOUT=" + utf8(png);
			auto const user_arg = "-USERDIR=" + utf8(Directory);
			auto const data_arg = "-DATADIR=" + Data_Directory();
			char const * args[] = {"-EDITORCAPTURE", map_arg.c_str(), png_arg.c_str(), user_arg.c_str(), data_arg.c_str(), nullptr};
			Child = Main_Start_Game_Process(args);
			return(Child != nullptr);
		}

		bool Running() const { return(Child != nullptr); }
		void Cancel() { Stop(); }

		bool Poll(EditorViewImage & image, bool & success)
		{
			int result = 0;
			if (!Child || !Main_Poll_Game_Process(Child, result)) return(false);
			success = result == 0 && Read_Image(image);
			Stop();
			return(true);
		}

	private:
		void Stop()
		{
			Main_Stop_Game_Process(Child);
			Child = nullptr;
			if (!Directory.empty()) {
				std::error_code error;
				FS::remove_all(Directory, error);
				Directory.clear();
			}
		}

		bool Read_Image(EditorViewImage & image)
		{
			EditorViewImage next;
			std::ifstream metadata(Directory / "map.png.view");
			if (!(metadata >> next.OriginX >> next.OriginY >> next.HalfTileX >> next.HalfTileY)
				|| next.HalfTileX <= 0 || next.HalfTileY <= 0) return(false);
			std::ifstream png(Directory / "map.png", std::ios::binary);
			std::vector<unsigned char> const bytes(std::istreambuf_iterator<char>(png), {});
			if (bytes.empty() || bytes.size() > 256 * 1024 * 1024) return(false);
			if (!UI_Decode_Raster("editor map preview", bytes, next.Pixels, next.Width, next.Height, false)) return(false);
			image = std::move(next);
			return(true);
		}

		FS::path Directory;
		MainChildProcess * Child = nullptr;
};


struct EditorAsset
{
	int Tile = -1;
	std::string Name;
	std::string Category;
	std::string File;
	std::string Object;
};

struct EditorAssetImage
{
	int Width = 0;
	int Height = 0;
	int AnchorX = 0;
	int AnchorY = 0;
	std::vector<std::uint8_t> Pixels;
};

struct EditorStamp
{
	struct Cell { int X, Y, Index, Height; bool Water; };
	std::vector<Cell> Cells;
	EditorAssetImage Image;
};

std::vector<std::uint8_t> Read_Editor_Asset(std::string const & name, int limit)
{
	CCFileClass file(name.c_str());
	if (!file.Is_Available()) return {};
	int const size = file.Size();
	if (size <= 0 || size > limit) return {};
	std::vector<std::uint8_t> bytes(size);
	if (file.Read(bytes.data(), size) != size) return {};
	return(bytes);
}

int Asset_I32(std::vector<std::uint8_t> const & bytes, std::size_t offset)
{
	std::uint32_t const value = bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8)
		| (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
	return(static_cast<std::int32_t>(value));
}

void Plot_Asset_Pixel(EditorAssetImage & image, int x, int y, unsigned color, std::vector<std::uint8_t> const & palette)
{
	if (palette.size() != 768 || color == 0 || x < 0 || x >= image.Width || y < 0 || y >= image.Height) return;
	auto * pixel = image.Pixels.data() + (y * image.Width + x) * 4;
	for (int channel = 0; channel < 3; ++channel) pixel[channel] = std::min(255, palette[color * 3 + channel] * 4);
	pixel[3] = 255;
}

EditorStamp Read_Editor_Stamp(std::vector<std::uint8_t> const & bytes, std::vector<std::uint8_t> const & palette)
{
	EditorStamp stamp;
	if (bytes.size() < 20) return(stamp);
	int const columns = Asset_I32(bytes, 0);
	int const rows = Asset_I32(bytes, 4);
	int const width = Asset_I32(bytes, 8);
	int const height = Asset_I32(bytes, 12);
	if (columns < 1 || rows < 1 || columns > 32 || rows > 32 || columns * rows > 256
		|| (width != 60 && width != 48) || height != width / 2 || bytes.size() < 16 + 4u * columns * rows) return(stamp);

	auto & image = stamp.Image;
	image.Width = (columns + rows) * width / 2 + 256;
	image.Height = (columns + rows) * height / 2 + 320;
	image.AnchorX = rows * width / 2 + 128;
	image.AnchorY = 256;
	if (palette.size() == 768) image.Pixels.resize(image.Width * image.Height * 4);
	for (int index = 0; index < columns * rows; ++index) {
		int const offset = Asset_I32(bytes, 16 + 4 * index);
		if (offset == 0) continue;
		if (offset < 16 + 4 * columns * rows || std::size_t(offset) + 52 + height * height > bytes.size()) return {};
		int const x = index % columns;
		int const y = index / columns;
		int const elevation = bytes[offset + 40];
		if (elevation > 20) return {};
		stamp.Cells.push_back({x, y, index, elevation, bytes[offset + 41] == 9});
		if (image.Pixels.empty()) continue;
		int const center = image.AnchorX + (x - y) * width / 2;
		int const top = image.AnchorY + (x + y - elevation) * height / 2 - height / 2;
		int source = offset + 52;
		for (int row = 0; row < height; ++row) {
			int const span = 2 * std::min(row + 1, height - 1 - row);
			for (int px = -span; px < span; ++px) Plot_Asset_Pixel(image, center + px, top + row, bytes[source++], palette);
		}
		int const extra = Asset_I32(bytes, offset + 8);
		int const extra_width = Asset_I32(bytes, offset + 28);
		int const extra_height = Asset_I32(bytes, offset + 32);
		if (extra <= 0 || extra_width <= 0 || extra_width > 512 || extra_height <= 0 || extra_height > 512
			|| std::size_t(offset) + extra + extra_width * extra_height > bytes.size()) continue;
		auto const dx = std::int64_t(Asset_I32(bytes, offset + 20)) - Asset_I32(bytes, offset);
		auto const dy = std::int64_t(Asset_I32(bytes, offset + 24)) - Asset_I32(bytes, offset + 4);
		if (dx < -512 || dx > 512 || dy < -512 || dy > 512) continue;
		for (int row = 0; row < extra_height; ++row) {
			for (int px = 0; px < extra_width; ++px) {
				Plot_Asset_Pixel(image, center - width / 2 + int(dx) + px, top + int(dy) + row,
					bytes[std::size_t(offset) + extra + row * extra_width + px], palette);
			}
		}
	}
	if (!image.Pixels.empty()) {
		int left = image.Width, top = image.Height, right = 0, bottom = 0;
		for (int y = 0; y < image.Height; ++y) {
			for (int x = 0; x < image.Width; ++x) {
				if (!image.Pixels[(y * image.Width + x) * 4 + 3]) continue;
				left = std::min(left, x); top = std::min(top, y);
				right = std::max(right, x + 1); bottom = std::max(bottom, y + 1);
			}
		}
		if (right <= left || bottom <= top) image = {};
		else {
			EditorAssetImage cropped{right - left, bottom - top, image.AnchorX - left, image.AnchorY - top, {}};
			for (int y = top; y < bottom; ++y) {
				auto const first = image.Pixels.begin() + (y * image.Width + left) * 4;
				cropped.Pixels.insert(cropped.Pixels.end(), first, first + cropped.Width * 4);
			}
			image = std::move(cropped);
		}
	}
	return(stamp);
}

EditorAssetImage Read_Editor_Scenery(std::vector<std::uint8_t> const & bytes, std::vector<std::uint8_t> const & palette)
{
	if (bytes.size() < 32 || palette.size() != 768) return {};
	auto word = [&](int offset) { return(bytes[offset] + 256 * bytes[offset + 1]); };
	EditorAssetImage image{word(12), word(14), word(2) / 2 - word(8), word(4) / 2 - word(10), {}};
	int const offset = Asset_I32(bytes, 28);
	if (word(6) == 0 || image.Width < 1 || image.Width > 512 || image.Height < 1 || image.Height > 512
		|| offset < 32 || std::size_t(offset) >= bytes.size()) return {};
	image.Pixels.resize(image.Width * image.Height * 4);
	std::size_t position = offset;
	for (int y = 0; y < image.Height; ++y) {
		if ((bytes[16] & 2) == 0) {
			if (position + image.Width > bytes.size()) return {};
			for (int x = 0; x < image.Width; ++x) Plot_Asset_Pixel(image, x, y, bytes[position++], palette);
			continue;
		}
		if (position + 2 > bytes.size()) return {};
		auto const length = bytes[position] + 256 * bytes[position + 1];
		if (length < 2 || position + length > bytes.size()) return {};
		auto const end = position + length;
		position += 2;
		int x = 0;
		while (position < end && x < image.Width) {
			auto const color = bytes[position++];
			if (color) Plot_Asset_Pixel(image, x++, y, color, palette);
			else if (position < end) x += bytes[position++];
		}
		position = end;
	}
	return(image);
}

class MapEditorPreview
{
	public:
		void Load(std::string const & source)
		{
			BufferStraw input(source.data(), static_cast<int>(source.size()));
			INIClass ini;
			ini.Load(input);
			Rect const size = ini.Get_Rect("Map", "Size", Rect(0, 0, 50, 50));
			Width = std::clamp(size.Width, 1, 255);
			Height = std::clamp(size.Height, 1, 255);
			char theater[32];
			ini.Get_String("Map", "Theater", "TEMPERATE", theater, sizeof(theater));
			Snow = stricmp(theater, "SNOW") == 0;
			Cells.assign(512 * 512, 0);
			Trees.clear();
			Terrain.clear();

			Paintable = !ini.Get_Bool("Basic", "RandomMap", false);
			for (auto name : {"IsoMapPack", "IsoMapPack2", "IsoMapPack3", "IsoMapPack4"}) {
				if (ini.Entry_Count(name) > 0) Paintable = false;
			}
			if (CatalogTheater != theater) {
				CatalogTheater = theater;
				Load_Brushes(theater);
			}

			if (ini.Entry_Count("IsoMapPack5") > 0) {
				std::vector<unsigned char> packed(6 * 1024 * 1024);
				int const length = ini.Get_UUBlock("IsoMapPack5", packed.data(), static_cast<int>(packed.size()));
				BufferStraw bytes(packed.data(), length);
				LZOStraw records(LZOStraw::DECOMPRESS);
				records.Get_From(&bytes);
				TerrainRecord record{};
				int count = 0;
				while ((count = records.Get(record.data(), record.size())) == record.size()) {
					int const x = record[0] + 256 * record[1];
					int const y = record[2] + 256 * record[3];
					if (x == 0 && y == 0) break;
					if (x < 512 && y < 512) Terrain.insert_or_assign(y * 512 + x, record);
					if (Inside(x, y)) {
						unsigned int const tile = record[4] + 256u * record[5] + 65536u * record[6] + 16777216u * record[7];
						auto const brush = std::find(BrushTiles.begin(), BrushTiles.end(), static_cast<int>(tile));
						Cells[y * 512 + x] = tile != 0xffffffffu && brush != BrushTiles.end()
							? 256 + static_cast<int>(brush - BrushTiles.begin())
							: (tile == 0xffffffffu ? 0 : tile % 7) + 8 * std::clamp<int>(static_cast<signed char>(record[9]), 0, 20);
					}
				}
				bool const terminated = count >= 4 && record[0] == 0 && record[1] == 0 && record[2] == 0 && record[3] == 0;
				if (length <= 0 || length == static_cast<int>(packed.size()) || records.Is_Damaged() || (count != 0 && !terminated)) {
					Paintable = false;
				}
			}

			bool const modern = ini.Get_Int("Basic", "NewINIFormat", 0) >= 4;
			for (int i = 0; i < ini.Entry_Count("Terrain"); ++i) {
				auto const key = ini.Get_Entry("Terrain", i);
				if (!key) continue;
				int const cell = Read_Number(key);
				int const radix = modern ? 1000 : 128;
				if (cell > 0 && Inside(cell % radix, cell / radix)) Trees.push_back({cell % radix, cell / radix});
			}
		}

		EditorTileState Tile_State(int index) const
		{
			auto const record = Terrain.find(index);
			return(EditorTileState{record == Terrain.end() ? std::nullopt : std::optional(record->second), Cells[index]});
		}

		void Restore_Tile(int index, EditorTileState const & state)
		{
			if (state.Record) Terrain.insert_or_assign(index, *state.Record);
			else Terrain.erase(index);
			Cells[index] = state.Color;
		}

		bool Apply_Brush(int x, int y, int brush, int stamp_tile, int subtile, int elevation)
		{
			if (!Paintable) return(false);
			int const id = stamp_tile >= 0 ? stamp_tile : BrushTiles[brush];
			if (id < 0) return(false);
			int const index = y * 512 + x;
			auto & record = Terrain[index];
			unsigned int const tile = static_cast<unsigned int>(id);
			record[0] = static_cast<unsigned char>(x);
			record[1] = static_cast<unsigned char>(x >> 8);
			record[2] = static_cast<unsigned char>(y);
			record[3] = static_cast<unsigned char>(y >> 8);
			for (int byte = 0; byte < 4; ++byte) record[byte + 4] = static_cast<unsigned char>(tile >> (8 * byte));
			record[8] = static_cast<unsigned char>(subtile);
			if (elevation >= 0) record[9] = static_cast<unsigned char>(elevation);
			Cells[index] = 256 + (stamp_tile < 0 ? brush : 3);
			return(true);
		}

		bool Write_Terrain(std::string & source, std::string_view newline) const
		{
			std::string compressed;
			StringPipe output(compressed);
			LZOPipe lzo(LZOPipe::COMPRESS);
			lzo.Put_To(&output);
			for (auto const & [cell, record] : Terrain) lzo.Put(record.data(), record.size());
			unsigned char const terminator[4]{};
			lzo.Put(terminator, sizeof(terminator));
			lzo.End();

			INIClass ini;
			if (!ini.Put_UUBlock("IsoMapPack5", compressed.data(), static_cast<int>(compressed.size()))) return(false);
			std::string section;
			StringPipe section_output(section);
			ini.Save(section_output);
			return(Replace_Section(source, "isomappack5", section, newline));
		}

		bool Inside(int x, int y) const
		{
			return(x >= 0 && x < 512 && y >= 0 && y < 512 && std::abs(x - y) < Width
				&& x + y > Width && x + y <= Width + 2 * Height);
		}

		Point2D Hit(int pixel_x, int pixel_y) const
		{
			double const difference = pixel_x / 4.0 - Width;
			double const sum = pixel_y / 2.0 + Width;
			return(Point2D(static_cast<int>(std::lround((sum + difference) * 0.5)),
				static_cast<int>(std::lround((sum - difference) * 0.5))));
		}

		int Pixel_Width() const { return(8 * Width); }
		int Pixel_Height() const { return(4 * Height); }

		int Elevation(int x, int y) const
		{
			auto const record = Terrain.find(y * 512 + x);
			return(record == Terrain.end() ? 0 : static_cast<signed char>(record->second[9]));
		}

		Point2D Rendered_Hit(EditorViewImage const & image, int px, int py) const
		{
			double const difference = (px - image.OriginX) / static_cast<double>(image.HalfTileX);
			double const sum = (py - image.OriginY) / static_cast<double>(image.HalfTileY) - 1.0;
			for (int height = 32; height >= -8; --height) {
				int const x = static_cast<int>(std::lround((sum + difference + height) * 0.5));
				int const y = static_cast<int>(std::lround((sum - difference + height) * 0.5));
				if (Inside(x, y) && Elevation(x, y) == height) return(Point2D(x, y));
			}
			return(Point2D(-1, -1));
		}

		std::vector<std::uint8_t> Draw() const
		{
			int const width = Pixel_Width();
			int const height = Pixel_Height();
			std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
			auto plot = [&](int x, int y, int r, int g, int b) {
				if (x < 0 || x >= width || y < 0 || y >= height) return;
				auto * pixel = &pixels[(y * width + x) * 4];
				pixel[0] = static_cast<std::uint8_t>(r);
				pixel[1] = static_cast<std::uint8_t>(g);
				pixel[2] = static_cast<std::uint8_t>(b);
				pixel[3] = 255;
			};
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					auto const cell = Hit(x, y);
					if (!Inside(cell.X, cell.Y)) continue;
					int const code = Cells[cell.Y * 512 + cell.X];
					if (code >= 256) {
						auto const & color = Brushes[code - 256].Color;
						plot(x, y, color[0], color[1], color[2]);
						continue;
					}
					int const shade = (code / 8) * 4 + (code % 8) * 3 + ((cell.X + cell.Y) % 2) * 8;
					if (Snow) plot(x, y, 124 + shade, 142 + shade, 150 + shade);
					else plot(x, y, 47 + shade, 64 + shade, 43 + shade);
				}
			}
			auto marker = [&](int x, int y, int radius, int r, int g, int b) {
				int const px = (x - y + Width) * 4;
				int const py = (x + y - Width) * 2;
				for (int dy = -radius; dy <= radius; ++dy) {
					for (int dx = -radius; dx <= radius; ++dx) {
						int const d = dx * dx + dy * dy;
						if (d <= radius * radius) {
							bool const rim = d >= (radius - 2) * (radius - 2);
							plot(px + dx, py + dy, rim ? 12 : r, rim ? 21 : g, rim ? 22 : b);
						}
					}
				}
			};
			for (auto const & tree : Trees) marker(tree.X, tree.Y, 4, 78, 172, 103);
			return(pixels);
		}

		struct TileRange
		{
			int Base = -1;
			int Count = 0;
			bool Contains(int tile) const { return(Base >= 0 && tile >= Base && tile < Base + Count); }
		};
		std::array<std::pair<TileRange, TileRange>, 2> TransitionSets;
		TileRange WaterTiles, ClearTiles;
		std::array<TileRange, 4> TunnelTiles{};
		std::vector<std::pair<int, std::vector<EditorStamp::Cell>>> Shores;
		bool Terrain_Writable() const { return(Paintable); }

		int Tile_Id(int x, int y) const
		{
			auto const found = Terrain.find(y * 512 + x);
			if (found == Terrain.end()) return(BrushTiles[0]);
			auto const & r = found->second;
			return(static_cast<int>(r[4] | (unsigned(r[5]) << 8) | (unsigned(r[6]) << 16) | (unsigned(r[7]) << 24)));
		}

		std::vector<EditorAsset> Assets;
		std::string AssetSuffix, GeneralArchive, LocalArchive, IsoArchive, TheaterPalette;

		std::pair<EditorStamp, EditorAssetImage> Load_Asset(int index) const
		{
			auto const & asset = Assets[index];
			if (asset.File.empty()) return {};
			MixFileClass general(GeneralArchive.c_str(), &FastKey);
			MixFileClass local(LocalArchive.c_str(), &FastKey);
			MixFileClass iso(IsoArchive.c_str(), &FastKey);
			auto const bytes = Read_Editor_Asset(asset.File, 8 * 1024 * 1024);
			if (asset.Tile >= 0) {
				return {Read_Editor_Stamp(bytes, Read_Editor_Asset("ISO" + AssetSuffix + ".PAL", 768)), {}};
			}
			return {{}, Read_Editor_Scenery(bytes, Read_Editor_Asset(TheaterPalette, 768))};
		}

	private:
		void Load_Brushes(char const * name)
		{
			Assets.clear();
			TransitionSets = {};
			WaterTiles = ClearTiles = {};
			TunnelTiles = {};
			Shores.clear();
			BrushTiles.fill(-1);
			TheaterType const theater = TheaterClass::From_Name(name);
			if (theater == THEATER_NONE) return;
			auto const & data = TheaterClass::As_Reference(theater);
			AssetSuffix = data.Suffix.c_str();
			GeneralArchive = std::string(data.Root.c_str()) + ".MIX";
			TheaterPalette = std::string(data.Root.c_str()) + ".PAL";
			LocalArchive = AssetSuffix + ".MIX";
			IsoArchive = std::string(data.IsoRoot.c_str()) + ".MIX";
			MixFileClass general(GeneralArchive.c_str(), &FastKey);
			MixFileClass local(LocalArchive.c_str(), &FastKey);
			MixFileClass iso(IsoArchive.c_str(), &FastKey);
			auto const filename = std::string(data.Root.c_str()) + ".INI";
			CCFileClass file(filename.c_str());
			INIClass ini;
			if (ini.Load(file) <= 0) return;
			std::set<std::string> object_names;
			for (auto const * type : TerrainTypes) if (type) object_names.insert(type->Name());
			for (char const * filename : {"RULES.INI", "FIRESTRM.INI"}) {
				if (filename[0] == 'F' && !Addon_Enabled(ADDON_FIRESTORM)) continue;
				CCFileClass rules_file(filename);
				INIClass rules;
				if (rules.Load(rules_file) <= 0) continue;
				for (int i = 0; i < rules.Entry_Count("TerrainTypes"); ++i) {
					auto const key = rules.Get_Entry("TerrainTypes", i);
					char id[64];
					if (key && rules.Get_String("TerrainTypes", key, "", id, sizeof(id))) object_names.insert(id);
				}
			}
			for (auto const & id : object_names) {
				std::string file = id + '.' + AssetSuffix;
				if (!CCFileClass(file.c_str()).Is_Available()) file = id + ".SHP";
				if (!CCFileClass(file.c_str()).Is_Available()) continue;
				std::string const category = id.starts_with("TIBTRE") ? "Tiberium trees"
					: id.starts_with("TREE") ? "Trees" : "Scenery";
				Assets.push_back({-1, id, category, std::move(file), id});
			}
			int base = 0;
			for (int set = 0; set < 2048; ++set) {
				char section[32];
				std::snprintf(section, sizeof(section), "TileSet%04d", set);
				int const count = ini.Get_Int(section, "TilesInSet", -1);
				if (count < 0 || count > 65535) break;
				int const last = ini.Get_Int(section, "LastTilesInSet", -1);
				int const placeable_count = last < 0 ? count : std::min(count, last);
				for (std::size_t brush = 0; brush < BrushTiles.size(); ++brush) {
					if (placeable_count > 0 && ini.Get_Int("General", Brushes[brush].Key, -1) == set) BrushTiles[brush] = base;
				}
				if (ini.Get_Bool(section, "AllowToPlace", true)) {
					char category[64], prefix[64];
					ini.Get_String(section, "SetName", "Terrain", category, sizeof(category));
					ini.Get_String(section, "FileName", "TILE", prefix, sizeof(prefix));
					for (int tile = 0; tile < placeable_count && Assets.size() < 8192; ++tile) {
						char number[12];
						std::snprintf(number, sizeof(number), "%02d", tile + 1);
						std::string file = std::string(prefix) + number + '.' + AssetSuffix;
						if (!CCFileClass(file.c_str()).Is_Available()) {
							if (data.MMSuffix.empty()) continue;
							file = std::string(prefix) + number + '.' + data.MMSuffix.c_str();
							if (!CCFileClass(file.c_str()).Is_Available()) continue;
						}
						Assets.push_back({base + tile, std::string(category) + " " + number, category, std::move(file), {}});
					}
				}
				constexpr char const * primary[] = {"CrystalTile", "SwampTile"};
				constexpr char const * border[] = {"ClearToCrystalLat", "WaterToSwampLat"};
				for (std::size_t i = 0; i < TransitionSets.size(); ++i) {
					if (ini.Get_Int("General", primary[i], -1) == set) TransitionSets[i].first = {base, placeable_count};
					if (ini.Get_Int("General", border[i], -1) == set) TransitionSets[i].second = {base, placeable_count};
				}
				if (ini.Get_Int("General", "ClearTile", -1) == set) ClearTiles = {base, placeable_count};
				if (ini.Get_Int("General", "WaterSet", -1) == set) WaterTiles = {base, placeable_count};
				constexpr std::array tunnel_sets{"Tunnels", "TrackTunnels", "DirtTunnels", "DirtTrackTunnels"};
				for (std::size_t i = 0; i < tunnel_sets.size(); ++i)
					if (ini.Get_Int("General", tunnel_sets[i], -1) == set) TunnelTiles[i] = {base, std::min(4, placeable_count)};
				if (ini.Get_Int("General", "ShorePieces", -1) == set) {
					char prefix[64];
					ini.Get_String(section, "FileName", "TILE", prefix, sizeof(prefix));
					for (int i = 0; i < std::min(128, placeable_count); ++i) {
						char number[12];
						std::snprintf(number, sizeof(number), "%02d", i + 1);
						auto const bytes = Read_Editor_Asset(std::string(prefix) + number + '.' + AssetSuffix, 2 * 1024 * 1024);
						auto stamp = Read_Editor_Stamp(bytes, {});
						if (std::ranges::any_of(stamp.Cells, &EditorStamp::Cell::Water)
							&& !std::ranges::all_of(stamp.Cells, &EditorStamp::Cell::Water)) {
							Shores.emplace_back(base + i, std::move(stamp.Cells));
						}
					}
					std::stable_sort(Shores.begin(), Shores.end(), [](auto const & a, auto const & b) {return(a.second.size() > b.second.size());});
				}
				base += placeable_count;
			}
		}

		std::string CatalogTheater;
		std::map<int, TerrainRecord> Terrain;
		std::array<int, std::size(Brushes)> BrushTiles{};
		bool Paintable = false;
		int Width = 50;
		int Height = 50;
		bool Snow = false;
		std::vector<int> Cells;
		std::vector<Point2D> Trees;
};


struct OverlayCell
{
	unsigned char Type = 255;
	unsigned char Data = 0;
	bool operator==(OverlayCell const &) const = default;
};

bool Valid_LCW_Block(std::span<unsigned char const> bytes, unsigned size)
{
	if (bytes.empty()) return(false);
	bool const relative = bytes.front() == 0;
	unsigned out = 0;
	std::size_t p = relative ? 1 : 0;
	auto word = [&](std::size_t i) { return(unsigned(bytes[i]) | (unsigned(bytes[i + 1]) << 8)); };
	while (p < bytes.size()) {
		unsigned const op = bytes[p++];
		if (op == 0x80) return(out == size && p == bytes.size());
		unsigned count;
		if (op < 0x80) {
			if (p >= bytes.size()) return(false);
			unsigned const back = ((op & 15) << 8) | bytes[p++];
			if (back == 0 || back > out) return(false);
			count = (op >> 4) + 3;
		} else if (op < 0xc0) {
			count = op & 63;
			if (count > bytes.size() - p) return(false);
			p += count;
		} else if (op == 0xfe) {
			if (bytes.size() - p < 3) return(false);
			count = word(p);
			p += 3;
		} else {
			if (bytes.size() - p < (op == 0xff ? 4u : 2u)) return(false);
			count = op == 0xff ? word(p) : (op & 63) + 3;
			if (op == 0xff) p += 2;
			unsigned const offset = word(p);
			p += 2;
			if (relative ? (offset == 0 || offset > out) : offset >= out) return(false);
		}
		if (count == 0 || count > size - out) return(false);
		out += count;
	}
	return(false);
}

bool Read_Overlay_Layer(INIClass const & ini, char const * name, std::vector<unsigned char> & layer)
{
	if (ini.Entry_Count(name) == 0) return(true);
	std::vector<unsigned char> bytes(512 * 1024);
	int const length = ini.Get_UUBlock(name, bytes.data(), static_cast<int>(bytes.size()));
	if (length <= 0 || length == static_cast<int>(bytes.size())) return(false);
	std::size_t p = 0, out = 0;
	while (p < static_cast<std::size_t>(length)) {
		if (length - p < 4) return(false);
		unsigned const compressed = bytes[p] + 256u * bytes[p + 1];
		unsigned const expanded = bytes[p + 2] + 256u * bytes[p + 3];
		p += 4;
		if (compressed > length - p || expanded == 0 || expanded > 8192 || expanded > layer.size() - out
			|| !Valid_LCW_Block(std::span(bytes).subspan(p, compressed), expanded)) return(false);
		if (LCW_Uncomp(bytes.data() + p, layer.data() + out, expanded) != expanded) return(false);
		p += compressed;
		out += expanded;
	}
	return(out == layer.size());
}

bool Write_Overlay_Layer(std::string & source, std::string_view newline, char const * name, char const * key,
	std::vector<unsigned char> const & layer)
{
	std::string packed;
	StringPipe buffer(packed);
	LCWPipe pipe(LCWPipe::COMPRESS);
	pipe.Put_To(&buffer);
	pipe.Put(layer.data(), static_cast<int>(layer.size()));
	pipe.End();
	INIClass ini;
	if (!ini.Put_UUBlock(name, packed.data(), static_cast<int>(packed.size()))) return(false);
	std::string section;
	StringPipe text(section);
	ini.Save(text);
	return(Replace_Section(source, key, section, newline));
}

struct ClipboardCell
{
	EditorTileState Tile;
	OverlayCell Overlay;
};

bool Write_New_Map_File(FS::path const & path, std::string_view data, std::string & status)
{
#ifdef _WIN32
	std::FILE * file = _wfopen(path.c_str(), L"wbx");
#else
	std::FILE * file = std::fopen(path.c_str(), "wbx");
#endif
	if (!file) {
		status = "Could not create the map. Check the folder and existing files.";
		return(false);
	}
	auto const count = std::fwrite(data.data(), 1, data.size(), file);
	if (std::fclose(file) == 0 && count == data.size()) return(true);
	std::error_code error;
	FS::remove(path, error);
	status = "Could not write the map copy.";
	return(false);
}

struct LightingField
{
	std::string Label, Key, Value;
};

constexpr std::array<char const *, 6> LightKeys{"Ambient", "Red", "Green", "Blue", "Ground", "Level"};
constexpr std::array<char const *, 6> LightDefaults{"1", "1", "1", "1", "0.1", "0.01667"};
constexpr std::array<std::array<int, 2>, 8> TunnelSteps{{
	{0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1},
}};

using EditorRecords = std::vector<std::pair<std::string, std::string>>;

EditorRecords Read_Editor_Records(std::string_view text)
{
	EditorRecords records;
	while (!text.empty()) {
		auto const end = text.find('\n');
		auto const line = Trim(text.substr(0, end));
		auto const equals = line.find('=');
		if (line.starts_with(';')) records.emplace_back("", line);
		else if (equals != line.npos) records.emplace_back(Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
		if (end == text.npos) break;
		text.remove_prefix(end + 1);
	}
	return(records);
}

std::string Write_Editor_Records(EditorRecords const & records)
{
	std::string text;
	for (auto const & [key, value] : records) {
		if (!key.empty()) text += key + '=';
		text += value + '\n';
	}
	return(text);
}

std::vector<std::string> Split_Map_Record(std::string_view value)
{
	std::vector<std::string> fields;
	for (;;) {
		auto const end = value.find(',');
		fields.emplace_back(Trim(value.substr(0, end)));
		if (end == value.npos) return(fields);
		value.remove_prefix(end + 1);
	}
}

std::string Join_Map_Record(std::vector<std::string> const & fields)
{
	std::string value;
	for (std::size_t i = 0; i < fields.size(); ++i) {
		if (i != 0) value += ',';
		value += fields[i];
	}
	return(value);
}

std::string New_Record_Key(EditorRecords const & records)
{
	std::set<std::string> used;
	for (auto const & record : records) used.insert(record.first);
	for (std::size_t i = 0; i <= records.size(); ++i) {
		auto name = std::to_string(i);
		if (!used.contains(name)) return(name);
	}
	return {};
}

class MapEditorPresenter final : public UIPresenterClass
{
	public:
		MapEditorPresenter()
		{
			auto const user_path = CDFileClass::User_Path();
			if (!user_path) return;
			std::string_view const directory(user_path);
			RecoveryPath = FS::path(std::u8string(directory.begin(), directory.end())) / "map-editor-recovery.draft";
			std::error_code error;
			RecoveryAvailable = !RecoveryPath.empty() && FS::is_regular_file(RecoveryPath, error);
		}

		void Execute(UIIntent const & intent) override
		{
			if (PlaytestLogOpen) {
				if (intent.Name == "cancel") PlaytestLogOpen = false;
				else if (intent.Name == "log-record" && intent.Value >= 0
					&& static_cast<std::size_t>(intent.Value) < PlaytestLog.size()) {
					PlaytestLogOpen = false;
					Scripts = true;
					VisualScript = TriggerInspector = false;
					Select_Script_Section(0);
					TriggerToFocus = PlaytestLog[intent.Value].Id;
				}
				return;
			}
			if (TestSetup) {
				if (intent.Name == "cancel") TestSetup = false;
				else if (intent.Name == "launch-test") Test_Play();
				return;
			}
			if (BalanceOpen) {
				if (intent.Name == "cancel") BalanceOpen = false;
				return;
			}
			if (!PlaytestIssues.empty()) {
				if (intent.Name == "cancel") PlaytestIssues.clear();
				else if (intent.Name == "inspect-issue" && intent.Value >= 0
					&& static_cast<std::size_t>(intent.Value) < PlaytestIssues.size()) {
					auto issue = PlaytestIssues[intent.Value];
					PlaytestIssues.clear();
					if (issue.Section >= 0) {
						Scripts = true;
						VisualScript = TriggerInspector = false;
						Select_Script_Section(issue.Section);
						TriggerToFocus = issue.Key;
					}
					if (issue.Cell >= 0) {
						Point2D const cell(issue.Cell % 1000, issue.Cell / 1000);
						if (Preview.Inside(cell.X, cell.Y)) {
							auto const point = Project_Cell_Overview(cell);
							Center(point.x, point.y);
						}
					}
					Status = issue.Text;
				}
				else if (intent.Name == "fix-start" && intent.Value >= 0
					&& intent.Value < static_cast<int>(PlayerStartColors.size())) {
					PlaytestIssues.clear();
					Clear_Object_Selection();
					ToolTab = "waypoints";
					TileMode = false;
					SelectedAsset = -1;
					Select_Waypoint(intent.Value);
					auto const row = std::ranges::find(Rows, intent.Value, &WaypointRow::Slot);
					if (row != Rows.end() && Preview.Inside(row->X, row->Y)) {
						auto const point = Project_Cell_Overview(Point2D(row->X, row->Y));
						Center(point.x, point.y);
					}
					Status = "Click a cell to place player " + std::to_string(intent.Value + 1) + '.';
				}
				return;
			}
			if (!PendingAction.empty()) {
				if (intent.Name == "discard") {
					std::string const action = std::exchange(PendingAction, {});
					Apply_File_Action(action);
				} else if (intent.Name == "cancel" || intent.Name == "keep-editing") {
					PendingAction.clear();
				}
				return;
			}
			if (intent.Name == "stroke-end") {
				Finish_Stroke();
				return;
			}
			if (intent.Name == "stroke-start" || intent.Name == "stroke-move") {
				Paint_Stroke(intent.Value, intent.Name == "stroke-start");
				return;
			}
			Finish_Stroke();
			if (intent.Name == "undo" || intent.Name == "redo") {
				Move_History(intent.Name == "redo");
			} else if (intent.Name == "cancel") {
				if (MovingObject) {
					MovingObject = false;
					Status = "Move cancelled.";
				} else if (PickScriptPoint) {
					PickScriptPoint = false;
					Scripts = true;
				} else if (Scripts) {
					Scripts = false;
					TriggerToFocus.clear();
					if (ToolTab == "cell-tags") Load_Cell_Tags();
					Status = "Back to the map.";
				} else if (Setup || !FilePanel.empty()) {
					Setup = false;
					FilePanel.clear();
					Status = "Open an existing map or start a new one.";
				} else {
					Request_File_Action("exit");
				}
			} else if (intent.Name == "open-panel" || intent.Name == "export-panel") {
				Clear_Object_Selection();
				std::string const panel = intent.Name == "open-panel" ? "open" : "export";
				if (panel == "export" && !Loaded) return;
				FilePanel = FilePanel == panel ? "" : panel;
				Setup = Scripts = false;
			} else if (intent.Name == "new") {
				Clear_Object_Selection();
				FilePanel.clear();
				Scripts = false;
				Setup = true;
				Status = "Set the dimensions and theater for your new map.";
			} else if (intent.Name == "scripts" && Loaded) {
				Clear_Object_Selection();
				FilePanel.clear();
				Setup = false;
				Scripts = true;
				Select_Script_Section(ScriptIndex);
			} else if (intent.Name == "trigger-inspector" && Scripts) {
				TriggerInspector = !TriggerInspector;
				if (TriggerInspector) {
					VisualScript = false;
					Find_Triggers();
				}
			} else if (Scripts && TriggerInspector && intent.Name == "trigger-search") {
				Find_Triggers();
			} else if (Scripts && TriggerInspector && intent.Name == "trigger-select" && intent.Value >= 0
				&& static_cast<std::size_t>(intent.Value) < TriggerChoices.size()) {
				InspectedTrigger = TriggerChoices[intent.Value].Id;
				Load_Inspected_Trigger();
			} else if (Scripts && TriggerInspector && (intent.Name == "edit-event" || intent.Name == "edit-action")) {
				Select_Trigger_Step(intent.Name == "edit-action", intent.Value);
			} else if (Scripts && TriggerInspector && (intent.Name == "step-apply" || intent.Name == "step-up" || intent.Name == "step-down")) {
				Edit_Trigger_Step(intent.Name);
			} else if (intent.Name == "visual-script" && Scripts) {
				TriggerInspector = false;
				VisualScript = !VisualScript;
			} else if (intent.Name == "visual-apply" && Scripts && VisualScript) {
				Apply_Visual_Script();
			} else if (intent.Name == "visual-point" && Scripts && VisualScript) {
				PickScriptPoint = true;
				Scripts = false;
				Status = "Click an existing waypoint on the map.";
			} else if (intent.Name == "script-section" && Scripts) {
				Select_Script_Section(intent.Value);
			} else if (intent.Name == "apply-script" && Scripts) {
				Apply_Script_Section();
			} else if (intent.Name == "snow") {
				Snow = !Snow;
			} else if (intent.Name == "cell" && Loaded) {
				int const x = intent.Value % 1000;
				int const y = intent.Value / 1000;
				if (!Preview.Inside(x, y)) return;
				if (PickScriptPoint) {
					auto const hit = std::ranges::find_if(Rows, [&](auto const & row) { return row.X == x && row.Y == y; });
					if (hit == Rows.end()) {
						Status = "Click an existing waypoint or press Escape.";
						return;
					}
					VisualWaypoint = std::to_string(hit->Slot);
					PickScriptPoint = false;
					Scripts = true;
					return;
				}
				if (ToolTab == "actors") { Actor_At(x, y); return; }
				if (ToolTab == "cell-tags") {
					if (TagInspect) {
						auto const found = TagCells.find(intent.Value);
						if (found == TagCells.end()) Status = "There is no tag on that cell.";
						else {
							CellTag = found->second;
							TagChoicesChanged = true;
							Open_Tag_Trigger();
						}
					} else {
						Paint_Cell_Tag(x, y);
						Finish_Stroke();
					}
					return;
				}
				if (ToolTab == "tunnels") { Load_Tunnel(x, y); return; }
				if (ToolTab == "overlays") { Paint_Overlay(x, y); Finish_Stroke(); return; }
				if (ToolTab == "regions") { Region_Click(x, y); return; }
				if (ToolTab == "lighting" || ToolTab == "bounds" || ToolTab == "houses") return;
				if (ToolTab == "scenery" && InspectScenery) {
					Edit_Object(intent.Value, false);
					return;
				}
				if (TileMode) {
					Paint(x, y);
					return;
				}
				Slot = Slot.empty() ? "0" : Slot;
				X = std::to_string(x);
				Y = std::to_string(y);
				FieldsChanged = true;
				Edit(false);
			} else if (intent.Name == "tiles" || intent.Name == "scenery" || intent.Name == "waypoints"
				|| intent.Name == "actors" || intent.Name == "tunnels" || intent.Name == "lighting"
				|| intent.Name == "overlays" || intent.Name == "regions" || intent.Name == "houses" || intent.Name == "bounds"
				|| intent.Name == "cell-tags") {
				bool const switching_preview = IonLighting && (ToolTab == "lighting" || intent.Name == "lighting");
				ToolTab = intent.Name;
				ActorKey.clear();
				TunnelRoute.clear();
				TunnelKey.clear();
				ForceNewTunnel = false;
				if (switching_preview) Mark_Render_Pending();
				if (ToolTab == "lighting") Load_Lighting();
				if (ToolTab == "actors") Load_Actor_Choices();
				if (ToolTab == "houses") Load_Houses();
				if (ToolTab == "bounds") Load_Bounds();
				if (ToolTab == "cell-tags") Load_Cell_Tags();
				PreviewChanged = true;
				AssetCategory.clear();
				AssetSearch.clear();
				Clear_Object_Selection();
				TileMode = ToolTab != "waypoints";
				SelectedAsset = -1;
				AssetPage = 0;
			} else if (intent.Name == "overlay-brush" && ToolTab == "overlays" && intent.Value >= 0 && intent.Value < 4) {
				OverlayBrush = intent.Value;
			} else if (intent.Name == "region-origin" && ToolTab == "regions" && !RegionPaste) {
				RegionFirst = Point2D(intent.Value % 1000, intent.Value / 1000);
				RegionLast.reset();
			} else if (intent.Name == "copy-region" && ToolTab == "regions") {
				Copy_Region();
			} else if ((intent.Name == "paste-region" || intent.Name == "move-region") && ToolTab == "regions") {
				Paste_Region(intent.Name == "move-region");
			} else if (intent.Name == "save-group" && ToolTab == "regions") {
				Save_Group();
			} else if (intent.Name == "open-group" && ToolTab == "regions") {
				Open_Group();
			} else if (intent.Name == "select-region" && ToolTab == "regions") {
				RegionPaste = false;
				RegionFirst.reset(); RegionLast.reset(); RegionTarget.reset();
				PreviewChanged = true;
				Status = "Click the first corner of the region.";
			} else if (intent.Name == "use-clipboard" && ToolTab == "regions" && !Clipboard.empty()) {
				RegionPaste = true;
				RegionTarget.reset();
				PreviewChanged = true;
				Status = "Click the destination anchor to preview the pasted region.";
			} else if (intent.Name == "water-brush" && ToolTab == "tiles") {
				WaterBrush = !WaterBrush;
				SelectedAsset = -1;
				Status = WaterBrush ? "Click or drag to paint water." : "Clear ground brush selected.";
			} else if (intent.Name == "auto-shore" && ToolTab == "tiles") {
				AutoShore = !AutoShore;
			} else if (intent.Name == "load-house" && ToolTab == "houses") {
				Load_House_Fields();
			} else if (intent.Name == "apply-house" && ToolTab == "houses") {
				Apply_House();
			} else if ((intent.Name == "preview-bounds" || intent.Name == "apply-bounds") && ToolTab == "bounds") {
				Preview_Bounds(intent.Name == "apply-bounds");
			} else if (intent.Name == "balance" && Loaded) {
				Show_Balance();
			} else if (intent.Name == "tag-erase" && ToolTab == "cell-tags") {
				TagErase = !TagErase;
			} else if (intent.Name == "tag-inspect" && ToolTab == "cell-tags") {
				TagInspect = !TagInspect;
			} else if (intent.Name == "tag-layer" && ToolTab == "cell-tags") {
				ShowTags = !ShowTags;
				PreviewChanged = true;
			} else if (intent.Name == "tag-trigger" && ToolTab == "cell-tags") {
				Open_Tag_Trigger();
			} else if ((intent.Name == "actor-drag" || intent.Name == "actor-duplicate") && ToolTab == "actors" && ActorInspect) {
				Move_Actor(Point2D(intent.Value % 1000, intent.Value / 1000), intent.Name == "actor-duplicate");
			} else if (intent.Name == "actor-category" && ToolTab == "actors" && intent.Value >= 0
				&& intent.Value < static_cast<int>(std::size(ActorFormats))) {
				ActorCategory = intent.Value;
				PreviewChanged |= !ActorKey.empty();
				ActorKey.clear();
				ActorType = ActorFormats[ActorCategory].Default;
				ActorSpotStatus.clear();
				Load_Actor_Choices();
			} else if (intent.Name == "actor-mode" && ToolTab == "actors") {
				ActorInspect = intent.Value != 0;
				PreviewChanged |= !ActorKey.empty();
				ActorKey.clear();
			} else if (intent.Name == "apply-actor" && ToolTab == "actors" && ActorInspect) {
				Edit_Actor(false);
			} else if (intent.Name == "remove-actor" && ToolTab == "actors" && ActorInspect) {
				Edit_Actor(true);
			} else if (intent.Name == "ion-lighting" && ToolTab == "lighting") {
				IonLighting = intent.Value != 0;
				Load_Lighting();
				Mark_Render_Pending();
			} else if (intent.Name == "apply-lighting" && ToolTab == "lighting") {
				Apply_Lighting();
			} else if (intent.Name == "auto-transition" && ToolTab == "tiles") {
				AutoTransition = !AutoTransition;
			} else if (intent.Name == "new-tunnel" && ToolTab == "tunnels") {
				TunnelRoute.clear(); TunnelKey.clear();
				ForceNewTunnel = true;
				PreviewChanged = true;
				Status = "Click the entrance cell for a new tunnel.";
			} else if (intent.Name == "trim-tunnel" && ToolTab == "tunnels" && !TunnelRoute.empty()) {
				TunnelRoute.pop_back();
				PreviewChanged = true;
			} else if (intent.Name == "reverse-tunnel" && ToolTab == "tunnels") {
				std::ranges::reverse(TunnelRoute);
				PreviewChanged = true;
			} else if ((intent.Name == "apply-tunnel" || intent.Name == "remove-tunnel") && ToolTab == "tunnels") {
				Apply_Tunnel(intent.Name == "remove-tunnel");
			} else if (intent.Name == "scenery-mode" && ToolTab == "scenery") {
				InspectScenery = intent.Value != 0;
				Clear_Object_Selection();
				Status = InspectScenery ? "Click a scenery object to select it." : "Choose scenery from the palette.";
			} else if (intent.Name == "move-object" && InspectScenery && SelectedObjectCell >= 0) {
				MovingObject = !MovingObject;
				Status = MovingObject ? "Click an empty destination cell." : "Move cancelled.";
			} else if (intent.Name == "remove-object" && InspectScenery && SelectedObjectCell >= 0) {
				Edit_Object(SelectedObjectCell, true);
			} else if (intent.Name == "asset-page") {
				AssetPage = std::clamp(AssetPage + intent.Value, 0, LastAssetPage);
			} else if (intent.Name == "asset" && intent.Value >= 0 && intent.Value < static_cast<int>(Preview.Assets.size())) {
				auto loaded = Preview.Load_Asset(intent.Value);
				if (ToolTab == "tiles" && loaded.first.Cells.empty()) {
					Status = "This tile has no readable footprint.";
					return;
				}
				WaterBrush = false;
				SelectedAsset = intent.Value;
				ActiveStamp = std::move(loaded.first);
				ActiveObject = std::move(loaded.second);
				Status = Preview.Assets[SelectedAsset].Name + " selected.";
			} else if (intent.Name == "brush-size") {
				BrushSize = std::clamp(BrushSize + intent.Value, 1, 5);
			} else if (intent.Name == "paint" && TileMode && Loaded) {
				int const x = Read_Number(Trim(X));
				int const y = Read_Number(Trim(Y));
				if (Preview.Inside(x, y)) Paint(x, y);
				else Status = "Choose a cell inside the map.";
			} else if (intent.Name == "recover" && !Loaded && RecoveryAvailable) {
				Recover_Draft();
			} else if (intent.Name == "open" || intent.Name == "blank" || intent.Name == "sample" || intent.Name == "mission-sample") {
				Request_File_Action(intent.Name);
			} else if (intent.Name == "lightning" && Loaded) {
				Add_Lightning();
			} else if (intent.Name == "zoom") {
				Set_Zoom(Zoom + intent.Value, 0.5f, 0.5f);
			} else if (intent.Name == "fit") {
				Set_Zoom(1, 0.5f, 0.5f);
			} else if (intent.Name == "grid") {
				Grid = !Grid;
				PreviewChanged = true;
			} else if (intent.Name == "test" && Loaded) {
				TestSetup = true;
			} else if (intent.Name == "playtest-log" && Loaded) {
				PlaytestLogOpen = true;
			} else if (intent.Name == "validate" && Loaded) {
				Validate_Map();
			} else if (Loaded && intent.Name == "save") {
				if (WorkingPath.empty()) { FilePanel = "save-as"; Scripts = Setup = false; }
				else if (Has_Unexported_Changes()) Save_Map(false, true);
			} else if (Loaded && intent.Name == "save-as") {
				FilePanel = "save-as";
				Scripts = Setup = false;
			} else if (Loaded && (intent.Name == "export" || intent.Name == "write-as")) {
				Save_Map(intent.Name == "export", false);
			} else if (intent.Name == "select" && intent.Value >= 0 && static_cast<std::size_t>(intent.Value) < Rows.size()) {
				Select_Waypoint(Rows[intent.Value].Slot);
			} else if (Loaded && intent.Name == "player-start" && intent.Value >= 0
				&& intent.Value < static_cast<int>(PlayerStartColors.size())) {
				Select_Waypoint(intent.Value);
				Status = "Click a cell to place player " + std::to_string(intent.Value + 1) + '.';
			} else if (Loaded && (intent.Name == "set" || intent.Name == "clear")) {
				Edit(intent.Name == "clear");
			}
		}

		void Refresh() override
		{
			if (RecoveryDue && !LastStrokeCell && std::chrono::steady_clock::now() >= *RecoveryDue) {
				Save_Recovery_Draft();
			}
			bool success = false;
			if (RenderJob.Poll(Artwork, success)) {
				if (!success && Artwork.Pixels.empty()) Status = "Map render unavailable. Showing the cell view.";
				if (success && !RenderPending && Stroke.empty()) PendingPaint.clear();
				PreviewChanged = true;
			}
			if (!RenderJob.Running() && RenderPending && std::chrono::steady_clock::now() >= RenderDue) {
				RenderPending = false;
				auto data = Build_Copy();
				if (data) {
					try {
						if (ToolTab == "lighting" && IonLighting) Use_Ion_Preview(*data);
						if (!RenderJob.Start(*data) && Artwork.Pixels.empty()) Status = "Could not start the map renderer.";
					} catch (std::exception const &) {
						if (Artwork.Pixels.empty()) Status = "Could not prepare the map preview.";
					}
				}
			}
		}

		std::string ToolTab = "tiles";
		std::string FilePanel;
		std::string PendingAction;
		int OverlayBrush = 0;
		std::string OverlayDensity = "6";
		bool WaterBrush = false, AutoShore = true;
		bool CopyTerrain = true, CopyOverlays = true, CopyObjects = false, RegionPaste = false;
		std::string GroupPath, GroupReferences;
		std::string PasteElevation = "0";
		std::vector<std::string> HouseChoices;
		std::vector<LightingField> HouseFields;
		std::string HouseName, LoadedHouse;
		bool HouseChanged = false, BoundsChanged = false;
		std::string BoundsWidth, BoundsHeight, BoundsX, BoundsY, BoundsLocalWidth, BoundsLocalHeight;

		bool Has_Clipboard() const { return(!Clipboard.empty()); }
		std::vector<LightingField> Lights;
		bool IonLighting = false;
		bool LightsChanged = false;
		bool AutoTransition = true;
		int ActorCategory = 0;
		std::string ActorSpot = "0";
		std::string ActorSpotStatus;
		Point2D DragPreview{-1, -1};
		Point2D DragFootprint{1, 1};
		std::set<int> DragBlocked;
		void Update_Drag_Preview(Point2D anchor, bool duplicate)
		{
			DragPreview = anchor;
			DragBlocked.clear();
			if (!ActorKey.empty() && Preview.Inside(anchor.X, anchor.Y))
				Actor_Blocked_Subcells(anchor, duplicate ? "" : ActorKey, &DragBlocked);
		}
		std::string CellTag;
		std::vector<EditorTagChoice> TagChoices;
		std::string TriggerToFocus;
		bool TagChoicesChanged = false;
		bool TagErase = false, ShowTags = true, TagInspect = false;
		std::string BalanceText;
		bool BalanceOpen = false;
		bool ActorInspect = false;
		bool ActorFieldsChanged = false;
		std::string ActorKey;
		Point2D ActorCell{-1, -1};
		std::vector<std::string> ActorTypes, ActorHouses;
		std::string ActorType = "MTNK", ActorOwner = "GDI", ActorHealth = "256", ActorFacing = "0", ActorTag = "None";
		std::vector<Point2D> TunnelRoute;
		std::string TunnelKey;
		bool ForceNewTunnel = false;
		bool RecoveryAvailable = false;
		std::vector<PlaytestIssue> PlaytestIssues;
		bool TestSetup = false;
		bool PlaytestLogOpen = false;
		std::vector<EditorTagChoice> PlaytestLog;

		bool ScriptedTest = false;
		bool VisualScript = false, PickScriptPoint = false;
		bool TriggerInspector = false;
		std::string TriggerSearch, InspectedTrigger;
		std::vector<EditorTagChoice> TriggerChoices, TriggerEvents, TriggerActions;
		std::string TriggerLinks;
		int SelectedStep = -1;
		bool SelectedStepAction = false;
		std::string StepType, StepCode, StepValue, StepWaypoint;
		std::string VisualKind = "trigger", VisualName, VisualHouse = "GDI",
			VisualEvent = "time", VisualDelay = "10", VisualAction = "lightning",
			VisualWaypoint, VisualTeam, VisualForce, VisualTeamScript;
		std::string TestSlot = "0", TestOpponents = "1";

		bool Has_Unexported_Changes() const
		{
			return(Loaded && (Draft || Revision != SavedRevision));
		}
		std::string MapTitle;
		std::string MapDetails;

		bool Can_Undo() const { return(!Undo.empty()); }
		bool Can_Redo() const { return(!Redo.empty()); }
		bool Can_Stroke() const
		{
			return(Loaded && (ToolTab == "overlays" || (ToolTab == "cell-tags" && !TagInspect) || (ToolTab == "tiles" && (SelectedAsset < 0 || ActiveStamp.Cells.size() == 1))));
		}

		float Ghost_Scale(float viewport_width) const
		{
			return(viewport_width * Zoom / (Has_Artwork() ? Artwork.Width : Preview.Pixel_Width() * 7.5f));
		}

		EditorAssetImage const & Ghost() const { return(ToolTab == "scenery" ? ActiveObject : ActiveStamp.Image); }

		bool Stamp_Fits(int x, int y) const
		{
			int const base = Preview.Elevation(x, y);
			return(std::ranges::all_of(ActiveStamp.Cells, [&](auto const & cell) {
				return(Preview.Inside(x + cell.X, y + cell.Y) && base + cell.Height <= 20);
			}));
		}

		void Mark_Render_Pending()
		{
			RenderPending = true;
			RenderDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(180);
		}

		void Set_Zoom(float zoom, float x, float y)
		{
			float const old = Zoom;
			Zoom = std::clamp(zoom, 1.0f, 8.0f);
			if (old == Zoom) return;
			Left += x / old - x / Zoom;
			Top += y / old - y / Zoom;
			Clamp_View();
		}

		void Pan(float x, float y)
		{
			float const old_left = Left;
			float const old_top = Top;
			float const maximum = 1.0f - 1.0f / Zoom;
			Left = std::clamp(Left + x / Zoom, 0.0f, maximum);
			Top = std::clamp(Top + y / Zoom, 0.0f, maximum);
			PreviewChanged |= Left != old_left || Top != old_top;
		}

		void Center(float x, float y)
		{
			Left = x - 0.5f / Zoom;
			Top = y - 0.5f / Zoom;
			Clamp_View();
		}

		Point2D Hit(float x, float y) const
		{
			x = Left + x / Zoom;
			y = Top + y / Zoom;
			if (Artwork.Pixels.empty()) return(Preview.Hit(x * Preview.Pixel_Width(), y * Preview.Pixel_Height()));
			return(Preview.Rendered_Hit(Artwork, x * Artwork.Width, y * Artwork.Height));
		}

		Rml::Vector2f Project_Cell(Point2D cell) const
		{
			auto const point = Project_Cell_Overview(cell);
			return(Rml::Vector2f((point.x - Left) * Zoom, (point.y - Top) * Zoom));
		}

		Rml::Vector2f Project_Cell_Overview(Point2D cell) const
		{
			float x, y;
			if (Has_Artwork()) {
				x = static_cast<float>(Artwork.OriginX + (cell.X - cell.Y) * Artwork.HalfTileX) / Artwork.Width;
				y = static_cast<float>(Artwork.OriginY + (cell.X + cell.Y + 1 - Preview.Elevation(cell.X, cell.Y)) * Artwork.HalfTileY) / Artwork.Height;
			} else {
				x = 0.5f + (cell.X - cell.Y) * 4.0f / Preview.Pixel_Width();
				y = (cell.X + cell.Y - Preview.Pixel_Width() / 8) * 2.0f / Preview.Pixel_Height();
			}
			return(Rml::Vector2f(x, y));
		}

		bool Has_Artwork() const { return(!Artwork.Pixels.empty()); }

		int Canvas_Width() const { return(Has_Artwork() ? std::min(Artwork.Width, 1280) : Preview.Pixel_Width()); }
		int Canvas_Height() const
		{
			return(Has_Artwork() ? std::max(1, Artwork.Height * Canvas_Width() / Artwork.Width) : Preview.Pixel_Height());
		}

		std::vector<std::uint8_t> Draw_View(bool minimap) const
		{
			int const width = minimap ? 192 : Canvas_Width();
			int const height = minimap ? std::max(1, 192 * Canvas_Height() / Canvas_Width()) : Canvas_Height();
			bool const artwork = Has_Artwork();
			int const source_width = artwork ? Artwork.Width : Preview.Pixel_Width();
			int const source_height = artwork ? Artwork.Height : Preview.Pixel_Height();
			auto const schematic = artwork ? std::vector<std::uint8_t>{} : Preview.Draw();
			auto const & source = artwork ? Artwork.Pixels : schematic;
			std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 4);
			float const left = minimap ? 0.0f : Left;
			float const top = minimap ? 0.0f : Top;
			float const zoom = minimap ? 1.0f : Zoom;
			auto adjusted = [zoom](int p, int extent, int source_extent, float start) {
				return(std::clamp(static_cast<int>((start + static_cast<float>(p) / (extent * zoom)) * source_extent), 0, source_extent - 1));
			};
			for (int y = 0; y < height; ++y) {
				int const sy = adjusted(y, height, source_height, top);
				for (int x = 0; x < width; ++x) {
					int const sx = adjusted(x, width, source_width, left);
					auto const src = source.data() + (sy * source_width + sx) * 4;
					auto * dst = result.data() + (y * width + x) * 4;
					std::copy_n(src, 4, dst);
				}
			}

			auto dot = [&](int x, int y, int red, int green, int blue) {
				if (x < 0 || x >= width || y < 0 || y >= height) return;
				auto * pixel = result.data() + (y * width + x) * 4;
				pixel[0] = red; pixel[1] = green; pixel[2] = blue; pixel[3] = 255;
			};
			auto project = [&](int sx, int sy) {
				return(Point2D(std::lround((static_cast<float>(sx) / source_width - left) * width * zoom),
					std::lround((static_cast<float>(sy) / source_height - top) * height * zoom)));
			};
			if (artwork && Grid && !minimap) {
				auto line = [&](Point2D a, Point2D b) {
					if (std::max(a.X, b.X) < 0 || std::min(a.X, b.X) >= width
						|| std::max(a.Y, b.Y) < 0 || std::min(a.Y, b.Y) >= height) return;
					int const length = std::max(std::abs(b.X - a.X), std::abs(b.Y - a.Y));
					for (int step = 0; step < length; ++step) {
						int const x = a.X + (b.X - a.X) * step / length;
						int const y = a.Y + (b.Y - a.Y) * step / length;
						if (x < 0 || x >= width || y < 0 || y >= height) continue;
						auto * pixel = result.data() + (y * width + x) * 4;
						pixel[0] = pixel[0] * 3 / 5 + 30;
						pixel[1] = pixel[1] * 3 / 5 + 68;
						pixel[2] = pixel[2] * 3 / 5 + 68;
					}
				};
				for (int y = 0; y < 512; ++y) {
					for (int x = 0; x < 512; ++x) {
						if (!Preview.Inside(x, y)) continue;
						int const sx = Artwork.OriginX + (x - y) * Artwork.HalfTileX;
						int const sy = Artwork.OriginY + (x + y + 1 - Preview.Elevation(x, y)) * Artwork.HalfTileY;
						auto const north = project(sx, sy - Artwork.HalfTileY);
						auto const east = project(sx + Artwork.HalfTileX, sy);
						auto const south = project(sx, sy + Artwork.HalfTileY);
						auto const west = project(sx - Artwork.HalfTileX, sy);
						line(north, east);
						line(north, west);
						if (!Preview.Inside(x + 1, y) || Preview.Elevation(x + 1, y) != Preview.Elevation(x, y)) line(east, south);
						if (!Preview.Inside(x, y + 1) || Preview.Elevation(x, y + 1) != Preview.Elevation(x, y)) line(west, south);
					}
				}
			}

			if (artwork && !minimap) {
				int const radius_x = std::max(1, static_cast<int>(Artwork.HalfTileX * width * zoom / source_width));
				int const radius_y = std::max(1, static_cast<int>(Artwork.HalfTileY * height * zoom / source_height));
				for (auto const & [index, brush] : PendingPaint) {
					auto const point = Project_Cell(Point2D(index % 512, index / 512));
					int const x = static_cast<int>(point.x * width);
					int const y = static_cast<int>(point.y * height);
					auto const & color = Brushes[brush].Color;
					for (int dy = -radius_y; dy <= radius_y; ++dy) {
						int const span = radius_x * (radius_y - std::abs(dy)) / radius_y;
						for (int dx = -span; dx <= span; ++dx) dot(x + dx, y + dy, color[0], color[1], color[2]);
					}
				}
			}
			if (!minimap && ToolTab == "tunnels") {
				std::optional<Point2D> prior;
				for (auto const & cell : TunnelRoute) {
					auto const p = Project_Cell(cell);
					Point2D const point(static_cast<int>(p.x * width), static_cast<int>(p.y * height));
					if (prior) {
						int const dx = point.X - prior->X, dy = point.Y - prior->Y;
						int const count = std::max(std::abs(dx), std::abs(dy));
						for (int i = 0; i < count; ++i) dot(prior->X + dx * i / count, prior->Y + dy * i / count, 129, 235, 255);
					}
					for (int i = -2; i <= 2; ++i) {
						dot(point.X + i, point.Y, 255, 225, 127);
						dot(point.X, point.Y + i, 255, 225, 127);
					}
					prior = point;
				}
			}
			if (!minimap && (ToolTab == "regions" || ToolTab == "bounds")) {
				std::array<Point2D, 4> corners;
				bool visible = false;
				if (ToolTab == "bounds" && BoundsPreview) {
					auto const & r = *BoundsPreview;
					corners = {Bounds_To_Cell(r.X, r.Y), Bounds_To_Cell(r.X + r.Width, r.Y),
						Bounds_To_Cell(r.X + r.Width, r.Y + r.Height), Bounds_To_Cell(r.X, r.Y + r.Height)};
					visible = true;
				} else if (ToolTab == "regions" && (RegionPaste ? RegionTarget.has_value() : RegionFirst && RegionLast)) {
					auto const a = RegionPaste ? *RegionTarget : *RegionFirst;
					auto const b = RegionPaste ? Point2D(a.X + ClipboardSize.X - 1, a.Y + ClipboardSize.Y - 1) : *RegionLast;
					corners = {a, Point2D(b.X, a.Y), b, Point2D(a.X, b.Y)};
					visible = true;
				}
				if (visible) {
					for (std::size_t i = 0; i < corners.size(); ++i) {
						auto const a = Project_Cell(corners[i]), b = Project_Cell(corners[(i + 1) % corners.size()]);
						int const x = int(a.x * width), y = int(a.y * height);
						int const dx = int(b.x * width) - x, dy = int(b.y * height) - y;
						int const steps = std::max(1, std::max(std::abs(dx), std::abs(dy)));
						for (int step = 0; step <= steps; ++step) dot(x + dx * step / steps, y + dy * step / steps, 126, 232, 255);
					}
				}
			}
			if (!minimap && ToolTab == "bounds") {
				for (auto const & cell : BoundsExcluded) {
					auto const point = Project_Cell(cell);
					int const x = int(point.x * width), y = int(point.y * height);
					for (int i = -3; i <= 3; ++i) {
						dot(x + i, y + i, 255, 115, 94);
						dot(x + i, y - i, 255, 115, 94);
					}
				}
			}
			if (!minimap && ToolTab == "overlays") {
				for (auto const & [index, overlay] : OverlayEdits) {
					auto const p = Project_Cell(Point2D(index % 512, index / 512));
					int const x = int(p.x * width), y = int(p.y * height);
					dot(x, y, overlay.Type == 255 ? 244 : 92, 220, 120);
				}
			}
			bool const selected_scenery = ToolTab == "scenery" && InspectScenery && SelectedObjectCell >= 0;
			bool const selected_actor = ToolTab == "actors" && ActorInspect && !ActorKey.empty();
			if (!minimap && ToolTab == "cell-tags" && ShowTags) {
				for (auto const & [cell, tag] : TagCells) {
					auto const p = Project_Cell(Point2D(cell % 1000, cell / 1000));
					int const x = int(p.x * width), y = int(p.y * height);
					for (int i = -4; i <= 4; ++i) {
						dot(x + i, y - 4, 255, 150, 210);
						dot(x - 4, y + i, 255, 150, 210);
					}
				}
			}
			if (!minimap && selected_actor && Preview.Inside(DragPreview.X, DragPreview.Y)) {
				for (int dy = 0; dy < DragFootprint.Y; ++dy) {
					for (int dx = 0; dx < DragFootprint.X; ++dx) {
						auto const p = Project_Cell(Point2D(DragPreview.X + dx, DragPreview.Y + dy));
						int const x = int(p.x * width), y = int(p.y * height);
						for (int i = -5; i <= 5; ++i) {
							int const d = (5 - std::abs(i)) / 2;
							bool const blocked = DragBlocked.contains((DragPreview.Y + dy) * 1000 + DragPreview.X + dx);
							int const r = blocked ? 255 : 150, g = blocked ? 100 : 241, b = blocked ? 90 : 255;
							dot(x + i, y + d, r, g, b);
							dot(x + i, y - d, r, g, b);
						}
					}
				}
			}
			if (!minimap && (selected_scenery || selected_actor)) {
				auto const center = Project_Cell(selected_actor ? ActorCell : Point2D(SelectedObjectCell % 1000, SelectedObjectCell / 1000));
				int const x = static_cast<int>(center.x * width), y = static_cast<int>(center.y * height);
				for (int dx = -8; dx <= 8; ++dx) {
					int const dy = (8 - std::abs(dx)) / 2;
					dot(x + dx, y + dy, 255, 229, 152);
					dot(x + dx, y - dy, 255, 229, 152);
				}
			}
			for (auto const & row : Rows) {
				if (!Preview.Inside(row.X, row.Y)) continue;
				auto const point = minimap ? Project_Cell_Overview(Point2D(row.X, row.Y)) : Project_Cell(Point2D(row.X, row.Y));
				int const x = static_cast<int>(point.x * width), y = static_cast<int>(point.y * height);
				bool const start = row.Slot < static_cast<int>(PlayerStartColors.size());
				bool const selected = row.Slot == SelectedSlot;
				auto const color = start ? PlayerStartColors[row.Slot] : std::array{122, 228, 150};
				int const radius = minimap ? 2 : start ? 5 : 3;
				for (int dy = -radius; dy <= radius; ++dy) {
					for (int dx = -radius; dx <= radius; ++dx) {
						if (!start && std::abs(dx) + std::abs(dy) > radius) continue;
						bool const border = std::abs(dx) == radius || std::abs(dy) == radius;
						if (selected && border) dot(x + dx, y + dy, 255, 255, 255);
						else dot(x + dx, y + dy, color[0], color[1], color[2]);
					}
				}
				if (minimap || !start) continue;
				auto const & digit = PlayerStartDigits[row.Slot];
				for (int dy = 0; dy < 5; ++dy) {
					for (int dx = 0; dx < 3; ++dx) {
						if (digit[dy] & (4 >> dx)) dot(x + dx - 1, y + dy - 2, 13, 22, 25);
					}
				}
			}

			if (minimap) {
				int const x0 = std::min(width - 1, static_cast<int>(Left * width));
				int const y0 = std::min(height - 1, static_cast<int>(Top * height));
				int const x1 = std::min(width - 1, static_cast<int>((Left + 1.0f / Zoom) * width));
				int const y1 = std::min(height - 1, static_cast<int>((Top + 1.0f / Zoom) * height));
				for (int x = x0; x <= x1; ++x) { dot(x, y0, 255, 229, 152); dot(x, y1, 255, 229, 152); }
				for (int y = y0; y <= y1; ++y) { dot(x0, y, 255, 229, 152); dot(x1, y, 255, 229, 152); }
			}
			return(result);
		}

		std::string Path, Slot, X, Y;
		std::string Width = "64";
		std::string Height = "64";
		std::string Name = "Untitled Map";
		bool Snow = false;
		bool Setup = false;
		float Zoom = 1.0f;
		bool Grid = false;
		std::string Delay = "10";
		std::string TestData;
		bool PreviewChanged = false;
		bool TileMode = true;
		int BrushSize = 1;
		int AssetPage = 0;
		int LastAssetPage = 0;
		int SelectedAsset = -1;
		unsigned CatalogRevision = 0;
		std::string AssetSearch;
		std::string AssetCategory;
		EditorStamp ActiveStamp;
		EditorAssetImage ActiveObject;
		bool InspectScenery = false;
		bool MovingObject = false;
		int SelectedObjectCell = -1;
		std::string SelectedObjectName;
		bool Scripts = false;
		int ScriptIndex = 0;
		std::string ScriptText;
		bool ScriptTextChanged = false;
		MapEditorPreview Preview;
		std::string Destination;
		bool Draft = false;
		std::string Status = "Enter the path to an existing .map or .mpr file.";
		std::vector<WaypointRow> Rows;
		int SelectedSlot = -1;
		bool Loaded = false;
		bool FieldsChanged = false;

	private:
		using HistoryEdit = std::function<void(bool)>;
		std::vector<HistoryEdit> Undo;
		std::vector<HistoryEdit> Redo;
		std::map<int, EditorTileChange> Stroke;
		std::map<int, OverlayCell> OverlayStroke;
		std::optional<EditorRecords> TagStroke;
		std::map<int, std::string> TagCells;
		int TagRadix = 1000;
		mutable std::map<std::string, Point2D> FoundationSizes;
		struct PlacedActor { int Section; std::string Key; Point2D Anchor, Size; int Spot; };
		std::vector<PlacedActor> PlacedActors;
		std::optional<std::size_t> PlacedActorsRevision;
		std::vector<unsigned char> OverlayTypesBase, OverlayDataBase;
		std::map<int, OverlayCell> OverlayEdits;
		std::map<std::string, std::string> HouseEdits;
		std::array<int, 2> ResourceValue{};
		std::array<unsigned char, 2> ResourceOverlay{};
		bool OverlayWritable = false;
		std::optional<Point2D> RegionFirst, RegionLast, RegionTarget;
		struct RegionObject { int Section; Point2D Offset; std::vector<std::string> Fields; std::string Key; };
		std::optional<std::size_t> GroupSourceRevision;
		std::vector<RegionObject> ClipboardObjects;
		Point2D ClipboardSize{0, 0};
		std::vector<ClipboardCell> Clipboard;
		bool ClipboardTerrain = false, ClipboardOverlays = false;
		std::string ClipboardTheater;
		std::optional<Rect> BoundsPreview;
		int BoundsPreviewWidth = 0;
		std::vector<Point2D> BoundsExcluded;
		std::map<int, int> PendingPaint;
		bool WasTerrainChanged = false;
		std::optional<Point2D> LastStrokeCell;
		FS::path RecoveryPath;
		FS::path WorkingPath;
		std::string SavedContents;
		std::size_t Revision = 0, SavedRevision = 0, NextRevision = 0;
		bool OwnsRecovery = false;
		std::optional<std::chrono::steady_clock::time_point> RecoveryDue;

		void Schedule_Recovery()
		{
			if (!RecoveryPath.empty() && !RecoveryDue) RecoveryDue = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		}

		void End_Recovery()
		{
			RecoveryDue.reset();
			if (!OwnsRecovery) return;
			std::error_code error;
			FS::remove(RecoveryPath, error);
			if (!error) {
				RecoveryAvailable = false;
				OwnsRecovery = false;
			}
		}

		void Save_Recovery_Draft()
		{
			RecoveryDue.reset();
			if (!Has_Unexported_Changes()) {
				End_Recovery();
				return;
			}
			std::string const previous_status = Status;
			auto const data = Build_Copy();
			Status = previous_status;
			if (!data || data->size() > 32 * 1024 * 1024) {
				Status = "Could not prepare a recovery draft. Use Export Copy to save your map.";
				return;
			}
			auto temporary = RecoveryPath;
			temporary += ".tmp";
			std::error_code error;
			FS::remove(temporary, error);
			std::string failure;
			if (Write_New_Map_File(temporary, *data, failure)) {
				FS::rename(temporary, RecoveryPath, error);
				if (!error) {
					OwnsRecovery = RecoveryAvailable = true;
					return;
				}
				FS::remove(temporary, error);
			}
			Status = "Could not save a recovery draft. Use Save As to save your map.";
		}

		void Recover_Draft()
		{
			std::error_code error;
			auto const size = FS::file_size(RecoveryPath, error);
			if (error || size == 0 || size > 32 * 1024 * 1024) {
				Status = "Could not read the recovery draft.";
				return;
			}
			std::ifstream stream(RecoveryPath, std::ios::binary);
			ScenarioDocument recovered;
			recovered.Source.assign(std::istreambuf_iterator<char>(stream), {});
			if (!stream || !recovered.Scan()) {
				Status = "Could not read the recovery draft.";
				return;
			}
			WorkingPath.clear();
			Document = std::move(recovered);
			auto const destination = (RecoveryPath.parent_path() / "Recovered Map.mpr").u8string();
			Destination.assign(destination.begin(), destination.end());
			Draft = OwnsRecovery = true;
			Reset_Fields();
			Status = "Recovery draft restored. Use Save As to keep it.";
		}

		void Remember(HistoryEdit edit)
		{
			constexpr std::size_t history_limit = 64;
			if (Undo.size() == history_limit) Undo.erase(Undo.begin());
			auto const previous = Revision;
			Revision = ++NextRevision;
			Undo.push_back([this, previous, current = Revision, edit = std::move(edit)](bool forward) {
				edit(forward);
				Revision = forward ? current : previous;
			});
			Redo.clear();
			Schedule_Recovery();
		}

		void Move_History(bool forward)
		{
			auto & from = forward ? Redo : Undo;
			auto & to = forward ? Undo : Redo;
			if (from.empty()) return;
			auto edit = std::move(from.back());
			from.pop_back();
			edit(forward);
			Clear_Object_Selection();
			ActorKey.clear();
			TunnelRoute.clear();
			TunnelKey.clear();
			if (ToolTab == "lighting") Load_Lighting();
			if (ToolTab == "bounds") Load_Bounds();
			if (ToolTab == "cell-tags") Load_Cell_Tags();
			if (Scripts && TriggerInspector) Find_Triggers();
			PreviewChanged = true;
			to.push_back(std::move(edit));
			Schedule_Recovery();
			Status = forward ? "Edit redone." : "Edit undone.";
		}

		void Remember_Scripts(std::map<int, std::string> before)
		{
			if (before == ScriptEdits) return;
			Remember([this, before = std::move(before), after = ScriptEdits](bool forward) {
				ScriptEdits = forward ? after : before;
				if (Scripts) Select_Script_Section(ScriptIndex);
				if (auto const updated = Build_Copy()) Preview.Load(*updated);
				PreviewChanged = true;
				Mark_Render_Pending();
			});
		}

		void Finish_Stroke()
		{
			LastStrokeCell.reset();
			if (!OverlayStroke.empty()) Finish_Overlay_Stroke();
			if (TagStroke) {
				Commit_Records(4, *TagStroke);
				TagStroke.reset();
				Load_Cell_Tags();
			}
			if (Stroke.empty()) return;
			if (AutoTransition) Apply_Terrain_Transitions();
			if (WaterBrush && AutoShore) Match_Shores();
			Remember([this, before_dirty = WasTerrainChanged, cells = std::move(Stroke)](bool forward) {
				for (auto const & [index, change] : cells) {
					auto const & state = forward ? change.After : change.Before;
					Preview.Restore_Tile(index, state);
					if (state.Color >= 256) PendingPaint[index] = state.Color - 256;
					else PendingPaint.erase(index);
				}
				TerrainChanged = forward || before_dirty;
				PreviewChanged = true;
				Mark_Render_Pending();
			});
			Stroke.clear();
			Mark_Render_Pending();
		}

		void Paint_Stroke(int value, bool start)
		{
			if (start) Finish_Stroke();
			if (!Can_Stroke() || (!start && !LastStrokeCell)) return;
			Point2D const cell(value % 1000, value / 1000);
			if (!Preview.Inside(cell.X, cell.Y)) return;
			if (!LastStrokeCell) {
				WasTerrainChanged = TerrainChanged;
				LastStrokeCell = cell;
			}
			auto const from = *LastStrokeCell;
			int const dx = cell.X - from.X;
			int const dy = cell.Y - from.Y;
			int const steps = std::max(std::abs(dx), std::abs(dy));
			for (int step = steps == 0 ? 0 : 1; step <= steps; ++step) {
				int const x = steps ? from.X + static_cast<int>(std::lround(double(dx) * step / steps)) : cell.X;
				int const y = steps ? from.Y + static_cast<int>(std::lround(double(dy) * step / steps)) : cell.Y;
				if (Preview.Inside(x, y)) {
					if (ToolTab == "overlays") Paint_Overlay(x, y);
					else if (ToolTab == "cell-tags") Paint_Cell_Tag(x, y);
					else Paint_Brush(x, y);
				}
			}
			LastStrokeCell = cell;
		}

		void Clamp_View()
		{
			float const maximum = 1.0f - 1.0f / Zoom;
			Left = std::clamp(Left, 0.0f, maximum);
			Top = std::clamp(Top, 0.0f, maximum);
			PreviewChanged = true;
		}

		EditorViewImage Artwork;
		EditorRenderJob RenderJob;
		std::chrono::steady_clock::time_point RenderDue;
		bool RenderPending = false;
		float Left = 0.0f;
		float Top = 0.0f;

		void Validate_Map(bool launch = false)
		{
			auto const draft = Build_Copy();
			if (!draft) return;
			BufferStraw input(draft->data(), static_cast<int>(draft->size()));
			INIClass map;
			map.Load(input);
			auto scan = [&](int section, auto check) {
				auto const name = ScriptSections[section].Name;
				for (int i = 0; i < map.Entry_Count(name) && PlaytestIssues.size() < 100; ++i) {
					auto const key = map.Get_Entry(name, i);
					if (key) check(std::string(key), Split_Map_Record(map.Get_String(name, key)));
				}
			};
			auto exists = [&](int section, std::string const & key) {
				return(map.Is_Present(ScriptSections[section].Name, key.c_str()));
			};
			auto valid_reference = [&](int section, std::string const & value, bool listed) {
				if (value == "-1") return(true);
				if (!listed) return(exists(section, value));
				auto const name = ScriptSections[section].Name;
				for (int i = 0; i < map.Entry_Count(name); ++i) {
					auto const key = map.Get_Entry(name, i);
					if (key && Equal_Section(map.Get_String(name, key), value)) return(true);
				}
				return(false);
			};
			for (int section : {1, 2}) scan(section, [&](auto const & key, auto const & fields) {
				if (!exists(0, key)) return;
				bool const action = section == 2;
				std::size_t const width = action ? 8 : 3;
				int const count = fields.empty() ? -1 : Read_Number(fields.front());
				auto report = [&](std::string detail, bool blocking = true) {
					PlaytestIssues.push_back({-1, std::string(ScriptSections[section].Name) + " " + key + " · "
						+ std::move(detail), section, key, -1, blocking});
				};
				auto const invalid_field = [](std::string const & value) { return value.empty(); };
				if (count < 0 || (fields.size() != 1 + static_cast<std::size_t>(count) * width
					&& !(action && count > 0 && fields.size() == static_cast<std::size_t>(count) * width))
					|| std::ranges::any_of(fields, invalid_field)) {
					report("Step count or fields are incomplete (count " + (fields.empty() ? std::string("missing") : fields.front()) + ").");
					return;
				}
				for (int step = 0; step < count && PlaytestIssues.size() < 100; ++step) {
					std::size_t const i = 1 + step * width;
					int const type = Read_Number(fields[i]), code = Read_Number(fields[i + 1]);
					auto const prefix = "Step " + std::to_string(step + 1) + " ";
					if (type < 0 || type >= (action ? int(TACTION_COUNT) : int(TEVENT_COUNT)))
						report(prefix + "has invalid type " + fields[i] + '.');
					if (code < 0 || code > (action ? 4 : 1)) {
						report(prefix + "has invalid parameter code " + fields[i + 1] + '.');
						continue;
					}
					auto const & value = fields[i + 2];
					int const target = action && code == 2 ? 0 : action && code == 3 ? 3 : 5;
					bool const team = target == 5;
					if (code != 0 && value != "-1") {
						if (action && value.size() < 3) {
							int const index = Read_Number(value);
							if (index < 0 || index >= map.Entry_Count(ScriptSections[target].Name))
								report(prefix + "has an out-of-range reference index " + value + '.');
						} else if (!valid_reference(target, value, team))
							report(prefix + "refers to missing " + ScriptSections[target].Name + " ID " + value + '.', false);
					}
					if (action && type == TACTION_ION_LIGHTNING_STRIKE) {
						auto const waypoint = i + 7 < fields.size() ? fields[i + 7] : std::string{};
						int const slot = waypoint.empty() ? -1 : Waypoint_From_Name(waypoint.c_str());
						if (std::ranges::find(Rows, slot, &WaypointRow::Slot) == Rows.end())
							report(prefix + "has no mapped lightning waypoint for value " + waypoint + '.', false);
					}
				}
			});
			if (launch) {
				std::erase_if(PlaytestIssues, [](auto const & issue) { return !issue.Blocking; });
				return;
			}

			scan(0, [&](auto const & key, auto const & fields) {
				if (!exists(1, key) || !exists(2, key))
					PlaytestIssues.push_back({-1, "Trigger " + key + " needs Events and Actions records.", 0, key});
				if (fields.size() >= 2 && !Equal_Section(fields[1], "<none>") && fields[1] != "0" && !exists(0, fields[1]))
					PlaytestIssues.push_back({-1, "Trigger " + key + " links to missing trigger " + fields[1] + '.', 0, key});
			});
			scan(3, [&](auto const & key, auto const & fields) {
				if (fields.size() < 3 || !exists(0, fields[2]))
					PlaytestIssues.push_back({-1, "Tag " + key + " links to a missing trigger.", 3, key});
			});
			scan(4, [&](auto const & key, auto const & fields) {
				int const cell = Read_Number(key), radix = Modern_Map() ? 1000 : 128;
				if (fields.empty() || !exists(3, fields.front()))
					PlaytestIssues.push_back({-1, "Cell tag " + key + " refers to a missing tag.", 4, key,
						cell < 0 ? -1 : cell / radix * 1000 + cell % radix});
			});
			scan(5, [&](auto const & key, auto const & fields) {
				if (fields.empty()) return;
				auto const & team = fields.front();
				for (auto const & [field, section] : {std::pair{"TaskForce", 6}, {"Script", 7}}) {
					auto const target = map.Get_String(team.c_str(), field);
					bool found = false;
					scan(section, [&](auto const &, auto const & entry) { found |= !entry.empty() && Equal_Section(entry[0], target); });
					if (!found) PlaytestIssues.push_back({-1, "Team " + team + " has invalid " + field + " = " + (target.empty() ? "(empty)" : target) + '.', 5, key});
				}
			});
			std::map<int, std::string> buildings;
			scan(11, [&](auto const & key, auto const & fields) {
				if (fields.size() < 5) return;
				int const x = Read_Number(fields[3]), y = Read_Number(fields[4]);
				if (!Preview.Inside(x, y)) return;
				auto const size = Structure_Footprint(fields[1]);
				bool blocked = false;
				for (int dy = 0; dy < size.Y; ++dy) for (int dx = 0; dx < size.X; ++dx) {
					int const cell = (y + dy) * 1000 + x + dx;
					blocked |= !buildings.emplace(cell, key).second;
				}
				if (blocked) PlaytestIssues.push_back({-1, "Structure " + key + " overlaps another structure.", -1, {}, y * 1000 + x});
			});
			std::set<int> starts;
			for (auto const & row : Rows) {
				if (!map.Get_Bool("Basic", "MultiplayerOnly", true) || row.Slot >= static_cast<int>(PlayerStartColors.size())) continue;
				int const cell = row.Y * 1000 + row.X;
				if (!Preview.Inside(row.X, row.Y) || !starts.insert(cell).second || buildings.contains(cell) || Spawn_Obstructed(row.X, row.Y))
					PlaytestIssues.push_back({row.Slot, "Check the bounds, overlap, or obstruction at P" + std::to_string(row.Slot + 1) + '.'});
			}
			scan(15, [&](auto const & key, auto const & fields) {
				if (fields.size() < 7) {
					PlaytestIssues.push_back({-1, "Tunnel " + key + " has an incomplete route.", 15, key});
					return;
				}
				int const x = Read_Number(fields[0]), y = Read_Number(fields[1]);
				int tx = x, ty = y;
				bool valid = Preview.Inside(x, y) && fields.back() == "-1" && fields[2] == fields[5];
				for (std::size_t i = 5; i + 1 < fields.size() && valid; ++i) {
					int const facing = Read_Number(fields[i]);
					if (facing < 0 || facing >= static_cast<int>(TunnelSteps.size())) { valid = false; break; }
					tx += TunnelSteps[facing][0]; ty += TunnelSteps[facing][1];
					valid = Preview.Inside(tx, ty);
				}
				if (!valid || tx != Read_Number(fields[3]) || ty != Read_Number(fields[4])) {
					PlaytestIssues.push_back({-1, "Tunnel " + key + " ends at " + std::to_string(tx) + ',' + std::to_string(ty)
						+ " · declared " + fields[3] + ',' + fields[4], 15, key, y * 1000 + x});
					return;
				}
				if (std::ranges::none_of(Preview.TunnelTiles, [](auto const & range) { return range.Base >= 0; })) return;
				for (auto const & cell : {Point2D(x, y), Point2D(tx, ty)}) {
					int const tile = Preview.Tile_Id(cell.X, cell.Y);
					if (std::ranges::none_of(Preview.TunnelTiles, [&](auto const & range) { return range.Contains(tile); }))
						PlaytestIssues.push_back({-1, "Tunnel " + key + " endpoint " + std::to_string(cell.X) + ','
							+ std::to_string(cell.Y) + " has tile " + std::to_string(tile) + " instead of tunnel art.",
							-1, {}, cell.Y * 1000 + cell.X});
				}
			});
			Status = PlaytestIssues.empty() ? "No map validation issues found." : "Select an issue to inspect it.";
		}

		bool Spawn_Obstructed(int x, int y)
		{
			int const index = y * 512 + x;
			if (Refresh_Placed_Actors() && std::ranges::any_of(PlacedActors, [&](PlacedActor const & actor) {
				return(actor.Section <= 11 && x >= actor.Anchor.X && y >= actor.Anchor.Y
					&& x < actor.Anchor.X + actor.Size.X && y < actor.Anchor.Y + actor.Size.Y);
			})) return(true);
			auto const type = Overlay_At(index).Type;
			if (type <= OVERLAY_WOOD_WALL || type == OVERLAY_FENCE || type == OVERLAY_NOD_WALL) return(true);
			return(Preview.WaterTiles.Contains(Preview.Tile_Id(x, y)));
		}

		void Test_Play()
		{
			Validate_Map(true);
			if (!PlaytestIssues.empty()) {
				TestSetup = false;
				Status = "Repair the listed script records before starting the playtest.";
				return;
			}
			auto const data = Build_Copy();
			if (!data) return;
			int const slot = Read_Number(TestSlot);
			int const opponents = Read_Number(TestOpponents);
			if (!ScriptedTest) {
				std::set<int> cells, slots;
				for (auto const & row : Rows) {
					if (row.Slot >= static_cast<int>(PlayerStartColors.size())) continue;
					if (Preview.Inside(row.X, row.Y) && !Spawn_Obstructed(row.X, row.Y)
						&& cells.insert(row.Y * 1000 + row.X).second) slots.insert(row.Slot);
					else PlaytestIssues.push_back({row.Slot, "P" + std::to_string(row.Slot + 1) + " has an invalid, obstructed, or duplicate start.", -1, {}, -1, true});
				}
				if (!PlaytestIssues.empty()) {
					TestSetup = false;
					Status = "Fix the player starts before launching.";
					return;
				}
				if (opponents < 1 || opponents > 7 || !slots.contains(slot) || static_cast<int>(slots.size()) <= opponents) {
					Status = "Choose an existing start and enough distinct starts for all opponents.";
					return;
				}
			} else {
				BufferStraw input(data->data(), static_cast<int>(data->size()));
				INIClass map;
				map.Load(input);
				auto const player = map.Get_String("Basic", "Player");
				if (map.Get_Bool("Basic", "MultiplayerOnly", false) || player.empty() || !map.Is_Present(player.c_str())) {
					Status = "Scripted play requires a single-player map with a Basic Player house section.";
					return;
				}
			}
			TestSetup = false;
			TestData = *data;
			PlaytestLog.clear();

			if (RenderJob.Running()) {
				RenderJob.Cancel();
				Mark_Render_Pending();
			}
			Result = UI_RESULT_ACCEPTED;
		}

		void Request_File_Action(std::string const & action)
		{
			if (Has_Unexported_Changes()) PendingAction = action;
			else Apply_File_Action(action);
		}

		void Apply_File_Action(std::string const & action)
		{
			if (action == "exit") {
				End_Recovery();
				Result = UI_RESULT_CANCELLED;
			} else if (action == "open") Open();
			else Create(action == "sample", action == "mission-sample");
		}

		void Create(bool sample, bool mission)
		{
			sample |= mission;
			int const width = sample ? 64 : Read_Number(Trim(Width));
			int const height = sample ? 64 : Read_Number(Trim(Height));
			if (width < 32 || width > 128 || height < 32 || height > 128) {
				Status = "Enter a width and height from 32 to 128.";
				return;
			}
			std::string const name = mission ? "Relay Outpost" : sample ? "Twin Groves" : std::string(Trim(Name));
			if (name.empty() || name.find_first_of(";\r\n") != name.npos) {
				Status = "Enter a name without a semicolon.";
				return;
			}
			auto document = mission ? Create_Mission_Sample() : Create_Scenario(width, height, name, !sample && Snow, sample);
			End_Recovery();
			Document = std::move(document);
			WorkingPath.clear();
			char const * directory = CDFileClass::User_Path();
			Destination = directory ? directory : "";
			Destination += name + ".mpr";
			Draft = true;
			Reset_Fields();
			ScriptedTest = mission;
			Schedule_Recovery();
			Status = mission ? "Destroy the Nod relay before the deadline. GDI reinforcements arrive after five seconds." : sample ? "Twin Groves loaded with two starting positions."
			                : "Click the map to place the selected waypoint.";
		}

		void Reset_Fields()
		{
			Loaded = true;
			PlaytestLog.clear();
			Setup = Scripts = PickScriptPoint = VisualScript = TriggerInspector = false;
			Changes.clear();
			ScriptEdits.clear();
			HouseEdits.clear();
			OverlayEdits.clear();
			OverlayStroke.clear();
			RegionFirst.reset(); RegionLast.reset(); RegionTarget.reset(); RegionPaste = false;
			BoundsPreview.reset();
			TerrainChanged = false;
			Preview.Load(Document.Source);
			Read_Overlays();
			++CatalogRevision;
			SelectedAsset = -1;
			Clear_Object_Selection();
			ActorKey.clear();
			TunnelRoute.clear();
			TunnelKey.clear();
			ForceNewTunnel = false;
			if (ToolTab == "lighting") Load_Lighting();
			if (ToolTab == "actors") Load_Actor_Choices();
			if (ToolTab == "houses") Load_Houses();
			if (ToolTab == "bounds") Load_Bounds();
			if (ToolTab == "cell-tags") Load_Cell_Tags();
			AssetPage = 0;
			AssetSearch.clear();
			AssetCategory.clear();
			FilePanel.clear();
			BufferStraw source(Document.Source.data(), static_cast<int>(Document.Source.size()));
			INIClass ini;
			ini.Load(source);
			char map_name[128];
			ini.Get_String("Basic", "Name", "Untitled Map", map_name, sizeof(map_name));
			MapTitle = map_name;
			Rect const size = ini.Get_Rect("Map", "Size", Rect(0, 0, 50, 50));
			char theater[32];
			ini.Get_String("Map", "Theater", "TEMPERATE", theater, sizeof(theater));
			MapDetails = std::to_string(size.Width) + " x " + std::to_string(size.Height) + "  |  " + theater;
			Zoom = 1;
			Left = Top = 0.0f;
			RenderJob.Cancel();
			Undo.clear();
			Redo.clear();
			Revision = SavedRevision = NextRevision = 0;
			PlacedActorsRevision.reset();
			GroupSourceRevision.reset();
			PendingPaint.clear();
			Artwork = {};
			Mark_Render_Pending();
			SelectedSlot = -1;
			Slot.clear();
			X.clear();
			Y.clear();
			FieldsChanged = true;
			Update_Rows();
		}

		void Open()
		{
			std::string_view path = Trim(Path);
			if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
			try {
				FS::path input = FS::path(std::u8string(path.begin(), path.end()));
				auto const ext = input.extension().string();
				if (!Equal_Section(ext, ".map") && !Equal_Section(ext, ".mpr")) {
					Status = "Choose a .map or .mpr file.";
					return;
				}
				std::error_code error;
				if (!FS::is_regular_file(input, error) || FS::file_size(input, error) > 32 * 1024 * 1024 || error) {
					Status = "The map must be a readable file of up to 32 MiB.";
					return;
				}
				std::ifstream stream(input, std::ios::binary);
				ScenarioDocument document;
				document.Source.assign(std::istreambuf_iterator<char>(stream), {});
				if (!stream || !document.Scan()) {
					Status = "Could not read the map or its waypoint records.";
					return;
				}
				WorkingPath = FS::absolute(input);
				SavedContents = document.Source;
				auto const extension = input.extension().native();
				input.replace_filename(input.stem()).concat("-edited").concat(extension);
				auto const suggested = input.u8string();
				Destination.assign(suggested.begin(), suggested.end());
				End_Recovery();
				Document = std::move(document);
				Draft = false;
				Reset_Fields();
				Status = "Map opened.";
			} catch (std::exception const &) {
				Status = "Could not open the map file.";
			}
		}

		void Select_Waypoint(int slot)
		{
			SelectedSlot = slot;
			Slot = std::to_string(slot);
			auto const row = std::ranges::find(Rows, slot, &WaypointRow::Slot);
			X = row == Rows.end() ? "" : std::to_string(row->X);
			Y = row == Rows.end() ? "" : std::to_string(row->Y);
			PreviewChanged = FieldsChanged = true;
		}

		void Edit(bool clear)
		{
			int const slot = Read_Number(Trim(Slot));
			int const x = Read_Number(Trim(X));
			int const y = Read_Number(Trim(Y));
			if (slot < 0 || slot >= WAYPT_COUNT) {
				Status = "Enter a waypoint ID from 0 to 100.";
				return;
			}
			if (!clear && (x < 0 || x > 511 || y < 0 || y > 511 || (x == 0 && y == 0))) {
				Status = "Enter coordinates from 0 to 511, excluding cell 0, 0.";
				return;
			}
			if (!clear && !Preview.Inside(x, y)) {
				Status = "Choose a cell inside the map.";
				return;
			}
			if (!clear && slot < static_cast<int>(PlayerStartColors.size()) && std::ranges::any_of(Rows, [&](auto const & row) {
				return(row.Slot != slot && row.Slot < static_cast<int>(PlayerStartColors.size()) && row.X == x && row.Y == y);
			})) {
				Status = "Another player starts at this cell.";
				return;
			}
			SelectedSlot = clear ? -1 : slot;
			int const cell = clear ? 0 : y * 1000 + x;
			int const original = Document.Waypoints[slot] ? Document.Waypoints[slot]->Cell : 0;
			auto const changed = Changes.find(slot);
			int const before = changed == Changes.end() ? original : changed->second;
			if (before == cell) return;
			Set_Waypoint(slot, cell, original);
			Remember([this, slot, before, cell, original](bool forward) {
				Set_Waypoint(slot, forward ? cell : before, original);
			});
			Status = Changes.empty() ? "No waypoint changes." : "Waypoint changes are ready to export.";
		}

		void Set_Waypoint(int slot, int cell, int original)
		{
			if (cell == original) Changes.erase(slot);
			else Changes.insert_or_assign(slot, cell);
			SelectedSlot = cell ? slot : -1;
			Slot = std::to_string(slot);
			X = cell ? std::to_string(cell % 1000) : "";
			Y = cell ? std::to_string(cell / 1000) : "";
			FieldsChanged = true;
			Update_Rows();
		}

		void Select_Script_Section(int index)
		{
			if (index < 0 || index >= static_cast<int>(std::size(ScriptSections))) return;
			ScriptIndex = index;
			auto const changed = ScriptEdits.find(index);
			if (changed != ScriptEdits.end()) {
				ScriptText = changed->second;
			} else {
				BufferStraw input(Document.Source.data(), static_cast<int>(Document.Source.size()));
				INIClass ini;
				ini.Load(input);
				ScriptText.clear();
				auto const section = ScriptSections[index].Name;
				for (int row = 0; row < ini.Entry_Count(section); ++row) {
					char const * key = ini.Get_Entry(section, row);
					if (!key) continue;
					char value[4096];
					int const length = ini.Get_String(section, key, "", value, sizeof(value));
					if (length >= static_cast<int>(sizeof(value)) - 1) {
						Status = "This section contains a row too long to edit.";
						ScriptAvailable = false;
						ScriptTextChanged = true;
						return;
					}
					ScriptText += std::string(key) + '=' + value + '\n';
				}
			}
			ScriptAvailable = true;
			Status = "Edit the INI records, then select Apply Section.";
			ScriptTextChanged = true;
		}

		void Apply_Script_Section()
		{
			if (!ScriptAvailable) return;
			std::string normalized;
			for (std::string_view text = ScriptText; !text.empty();) {
				auto const end = text.find('\n');
				auto line = Trim(text.substr(0, end));
				if (line.ends_with('\r')) line.remove_suffix(1);
				if (!line.empty() && !line.starts_with(';')) {
					auto const equals = line.find('=');
					if (line.starts_with('[') || line.find('\r') != line.npos || equals == line.npos || Trim(line.substr(0, equals)).empty()) {
						Status = "Use one key=value record per line. Leave out section headers.";
						return;
					}
				}
				normalized.append(line);
				normalized += '\n';
				if (end == text.npos) break;
				text.remove_prefix(end + 1);
			}
			auto before = ScriptEdits;
			ScriptEdits.insert_or_assign(ScriptIndex, std::move(normalized));
			Remember_Scripts(std::move(before));
			Mark_Render_Pending();
			Status = "Section applied. Save the map to keep it.";
		}

		void Read_Overlays()
		{
			OverlayTypesBase.assign(512 * 512, 255);
			OverlayDataBase.assign(512 * 512, 0);
			BufferStraw input(Document.Source.data(), static_cast<int>(Document.Source.size()));
			INIClass ini;
			ini.Load(input);
			OverlayWritable = ini.Get_Int("Basic", "NewINIFormat", 0) > 1
				&& Read_Overlay_Layer(ini, "OverlayPack", OverlayTypesBase)
				&& Read_Overlay_Layer(ini, "OverlayDataPack", OverlayDataBase);
			CCFileClass file("RULES.INI");
			INIClass rules;
			rules.Load(file);
			INIClass expansion;
			if (Addon_Enabled(ADDON_FIRESTORM)) {
				CCFileClass expanded_rules("FIRESTRM.INI");
				expansion.Load(expanded_rules);
			}
			constexpr std::array<unsigned char, 4> images{OVERLAY_TIBERIUM01, OVERLAY_LARGE_TIBERIUM01,
				OVERLAY_TIBERIUM2_01, OVERLAY_TIBERIUM3_01};
			for (std::size_t i = 0; i < ResourceValue.size(); ++i) {
				char const * name = i == 0 ? "Riparius" : "Vinifera";
				auto setting = [&](char const * key, int fallback) {
					return(ini.Get_Int(name, key, expansion.Get_Int(name, key, rules.Get_Int(name, key, fallback))));
				};
				ResourceValue[i] = std::max(0, setting("Value", i == 0 ? 25 : 40));
				int const image = std::clamp(setting("Image", i == 0 ? 1 : 3), 1, 4);
				ResourceOverlay[i] = images[image - 1];
			}
		}

		OverlayCell Overlay_At(int index) const
		{
			auto const item = OverlayEdits.find(index);
			if (item != OverlayEdits.end()) return(item->second);
			return OverlayCell{OverlayTypesBase[index], OverlayDataBase[index]};
		}

		void Set_Overlay(int index, OverlayCell cell)
		{
			if (cell == OverlayCell{OverlayTypesBase[index], OverlayDataBase[index]}) OverlayEdits.erase(index);
			else OverlayEdits.insert_or_assign(index, cell);
		}

		void Paint_Overlay(int x, int y)
		{
			int const density = Read_Number(Trim(OverlayDensity));
			if (!OverlayWritable) {
				Status = "Overlay painting requires readable packed overlays.";
				return;
			}
			if (OverlayBrush < 2 && (density < 0 || density > OVERLAYDATA_TIBERIUM_MAX)) {
				Status = "Enter a Tiberium growth stage from 0 to 11.";
				return;
			}
			unsigned char const types[] = {ResourceOverlay[0], ResourceOverlay[1], OVERLAY_BRICK_WALL, 255};
			OverlayCell const next{types[OverlayBrush], static_cast<unsigned char>(OverlayBrush < 2 ? density : 0)};
			int painted = 0;
			int const start = -(BrushSize - 1) / 2;
			for (int dy = start; dy < start + BrushSize; ++dy) {
				for (int dx = start; dx < start + BrushSize; ++dx) {
					if (!Preview.Inside(x + dx, y + dy)) continue;
					++painted;
					int const index = (y + dy) * 512 + x + dx;
					auto const old = Overlay_At(index);
					if (old == next) continue;
					OverlayStroke.try_emplace(index, old);
					Set_Overlay(index, next);
				}
			}
			PreviewChanged = true;
			Status = OverlayBrush < 2 ? "Brush value: " + std::to_string(1LL * painted * (density + 1) * ResourceValue[OverlayBrush]) + " credits."
				: "Overlay brush applied.";
		}

		void Finish_Overlay_Stroke()
		{
			std::set<int> candidates;
			constexpr std::array<std::array<int, 2>, 4> sides{{{0,-1}, {1,0}, {0,1}, {-1,0}}};
			for (auto const & [index, old] : OverlayStroke) {
				int const x = index % 512, y = index / 512;
				candidates.insert(index);
				for (auto const & step : sides) {
					if (Preview.Inside(x + step[0], y + step[1])) candidates.insert((y + step[1]) * 512 + x + step[0]);
				}
			}
			for (int index : candidates) {
				auto const old = Overlay_At(index);
				if (old.Type != OVERLAY_BRICK_WALL) continue;
				int const x = index % 512, y = index / 512;
				int mask = 0;
				for (std::size_t i = 0; i < sides.size(); ++i) {
					int const nx = x + sides[i][0], ny = y + sides[i][1];
					if (Preview.Inside(nx, ny) && Overlay_At(ny * 512 + nx).Type == old.Type) mask |= 1 << i;
				}
				auto const data = static_cast<unsigned char>((old.Data & 0xf0) | mask);
				if (data == old.Data) continue;
				OverlayStroke.try_emplace(index, old);
				Set_Overlay(index, {old.Type, data});
			}
			std::map<int, OverlayCell> after;
			for (auto const & [index, old] : OverlayStroke) after.emplace(index, Overlay_At(index));
			Remember([this, before = std::move(OverlayStroke), after = std::move(after)](bool forward) {
				for (auto const & [index, cell] : (forward ? after : before)) Set_Overlay(index, cell);
				PreviewChanged = true;
				Mark_Render_Pending();
			});
			OverlayStroke.clear();
			Mark_Render_Pending();
		}

		void Region_Click(int x, int y)
		{
			if (RegionPaste) {
				RegionTarget = Point2D(x, y);
				Status = "Previewing region. Choose Paste Here to commit.";
			} else if (!RegionFirst || RegionLast) {
				RegionFirst = Point2D(x, y);
				RegionLast.reset();
				Status = "Click the opposite corner of the region.";
			} else RegionLast = Point2D(x, y);
			PreviewChanged = true;
		}

		void Copy_Region()
		{
			if ((!CopyTerrain && !CopyOverlays && !CopyObjects) || !RegionFirst || !RegionLast || (CopyOverlays && !OverlayWritable)) {
				Status = "Select two corners and at least one readable layer.";
				return;
			}
			int const x = std::min(RegionFirst->X, RegionLast->X), y = std::min(RegionFirst->Y, RegionLast->Y);
			int const width = std::abs(RegionFirst->X - RegionLast->X) + 1;
			int const height = std::abs(RegionFirst->Y - RegionLast->Y) + 1;
			if (width * height > 16384 || !Preview.Inside(x, y) || !Preview.Inside(x + width - 1, y)
				|| !Preview.Inside(x, y + height - 1) || !Preview.Inside(x + width - 1, y + height - 1)) {
				Status = "Select up to 16,384 cells wholly inside the map.";
				return;
			}
			std::vector<RegionObject> objects;
			if (CopyObjects) {
				if (!Modern_Map()) {
					Status = "Object copying requires the current scenario format.";
					return;
				}
				for (int section = 10; section <= 14; ++section) {
					EditorRecords records;
					if (!Load_Records(section, records)) return;
					for (auto const & [key, value] : records) {
						if (key.empty()) continue;
						auto fields = Split_Map_Record(value);
						if (section != 10 && fields.size() <= static_cast<std::size_t>(Group_Tag_Field(section))) continue;
						int const cell = Read_Number(key);
						int const ox = section == 10 ? cell % 1000 : Read_Number(fields[3]);
						int const oy = section == 10 ? cell / 1000 : Read_Number(fields[4]);
						if (ox >= x && ox < x + width && oy >= y && oy < y + height)
							objects.push_back({section, Point2D(ox - x, oy - y), std::move(fields), key});
					}
				}
			}
			ClipboardObjects = std::move(objects);
			GroupSourceRevision = Revision;
			Describe_Group();
			Clipboard.clear();
			ClipboardSize = Point2D(width, height);
			ClipboardTheater = Preview.AssetSuffix;
			ClipboardTerrain = CopyTerrain;
			ClipboardOverlays = CopyOverlays;
			for (int dy = 0; dy < height; ++dy) {
				for (int dx = 0; dx < width; ++dx) {
					int const index = (y + dy) * 512 + x + dx;
					Clipboard.push_back({Preview.Tile_State(index), Overlay_At(index)});
				}
			}
			RegionPaste = true;
			RegionTarget.reset();
			PreviewChanged = true;
			Status = "Region copied. Click the destination anchor to preview it.";
		}

		void Paste_Region(bool move)
		{
			if (move && (ClipboardObjects.empty() || GroupSourceRevision != Revision)) {
				Status = "Select the objects again before moving them.";
				return;
			}
			if (!RegionTarget || Clipboard.empty()) {
				Status = "Click a destination before pasting.";
				return;
			}
			if (!move && ((ClipboardOverlays && !OverlayWritable) || (ClipboardTerrain && !Preview.Terrain_Writable()))) {
				Status = "The destination map does not support a copied layer.";
				return;
			}
			if (ClipboardTheater != Preview.AssetSuffix) {
				Status = "Paste into a map with the same theater.";
				return;
			}
			int const offset = Read_Number(Trim(PasteElevation));
			if (!move && ClipboardTerrain && (offset < -20 || offset > 20 || (offset == -1 && Trim(PasteElevation) != "-1"))) {
				Status = "Enter an elevation offset from -20 to 20.";
				return;
			}
			struct Edit { int Index; ClipboardCell Before, After; };
			std::vector<Edit> edits;
			for (int dy = 0; !move && dy < ClipboardSize.Y; ++dy) {
				for (int dx = 0; dx < ClipboardSize.X; ++dx) {
					int const x = RegionTarget->X + dx, y = RegionTarget->Y + dy;
					if (!Preview.Inside(x, y)) {
						Status = "The entire pasted region must fit inside the map.";
						return;
					}
					int const index = y * 512 + x;
					ClipboardCell const before{Preview.Tile_State(index), Overlay_At(index)};
					auto next = Clipboard[dy * ClipboardSize.X + dx];
					if (ClipboardTerrain) {
						int const level = (next.Tile.Record ? (*next.Tile.Record)[9] : 0) + offset;
						if (level < 0 || level > 20) {
							Status = "The elevation offset would put cells outside levels 0–20.";
							return;
						}
						if (next.Tile.Record || level != 0) {
							auto & r = next.Tile.Record ? *next.Tile.Record : next.Tile.Record.emplace(TerrainRecord{0,0,0,0,255,255,255,255});
							r[0] = x & 255; r[1] = x >> 8; r[2] = y & 255; r[3] = y >> 8; r[9] = level;
						}
					}
					edits.push_back({index, before, next});
				}
			}
			auto scripts = ScriptEdits;
			if (!ClipboardObjects.empty() && !Stage_Region_Objects(scripts, move)) return;
			auto apply = [this, terrain = ClipboardTerrain, overlays = ClipboardOverlays, prior = TerrainChanged,
				before_scripts = ScriptEdits, after_scripts = std::move(scripts),
				edits = std::move(edits)](bool forward) {
				for (auto const & edit : edits) {
					auto const & state = forward ? edit.After : edit.Before;
					if (terrain) Preview.Restore_Tile(edit.Index, state.Tile);
					if (overlays) Set_Overlay(edit.Index, state.Overlay);
				}
				TerrainChanged = prior || (terrain && forward);
				ScriptEdits = forward ? after_scripts : before_scripts;
				PreviewChanged = true;
				Mark_Render_Pending();
			};
			apply(true);
			Remember(std::move(apply));
			Status = move ? "Objects moved." : "Region pasted.";
		}

		bool Stage_Region_Objects(std::map<int, std::string> & scripts, bool move)
		{
			if (!Modern_Map()) {
				Status = "Object pasting requires the current scenario format.";
				return(false);
			}
			std::array<EditorRecords, 5> existing;
			EditorRecords tags;
			if (!Load_Records(3, tags)) return(false);
			std::map<int, unsigned> occupied;
			auto reserve = [&](int section, Point2D anchor, std::vector<std::string> const & fields, bool check) {
				auto const size = section == 11 ? Structure_Footprint(fields[1]) : Point2D(1, 1);
				int const spot = section == 13 ? Read_Number(fields[5]) : -1;
				unsigned const mask = spot >= 0 && spot < 5 ? 1u << spot : 31u;
				for (int dy = 0; dy < size.Y; ++dy) for (int dx = 0; dx < size.X; ++dx) {
					int const x = anchor.X + dx, y = anchor.Y + dy;
					if (!Preview.Inside(x, y) || (occupied[y * 1000 + x] & mask)) {
						if (check) {
							Status = "Object paste blocked at " + std::to_string(x) + ", " + std::to_string(y) + '.';
							return(false);
						}
					}
					occupied[y * 1000 + x] |= mask;
				}
				return(true);
			};
			for (int section = 10; section <= 14; ++section) {
				auto & records = existing[section - 10];
				if (!Load_Records(section, records)) return(false);
				if (move) std::erase_if(records, [&](auto const & record) {
					return(std::ranges::any_of(ClipboardObjects, [&](auto const & object) {
						return(object.Section == section && object.Key == record.first);
					}));
				});
				for (auto const & [key, value] : records) {
					if (key.empty()) continue;
					auto const fields = Split_Map_Record(value);
					int const cell = Read_Number(key);
					if (section == 10 && cell >= 0) reserve(section, Point2D(cell % 1000, cell / 1000), fields, false);
					else if (section != 10 && fields.size() >= 6)
						reserve(section, Point2D(Read_Number(fields[3]), Read_Number(fields[4])), fields, false);
				}
			}
			std::set<int> modified;
			for (auto const & object : ClipboardObjects) {
				int const section = object.Section;
				Point2D const cell(RegionTarget->X + object.Offset.X, RegionTarget->Y + object.Offset.Y);
				auto fields = object.Fields;
				if (!reserve(section, cell, fields, true)) return(false);
				auto & records = existing[section - 10];
				if (section == 10) records.emplace_back(std::to_string(cell.Y * 1000 + cell.X), Join_Map_Record(fields));
				else {
					fields[3] = std::to_string(cell.X);
					fields[4] = std::to_string(cell.Y);
					int const tag_index = Group_Tag_Field(section);
					auto const & tag = fields[tag_index];
					if (Group_Has_Tag(tag) && std::ranges::none_of(tags, [&](auto const & record) { return Equal_Section(record.first, tag); })) {
						Status = "Object needs tag " + tag + " in the destination map.";
						return(false);
					}
					if (!move && section == 12 && fields.size() > 11) fields[11] = "-1";
					records.emplace_back(move ? object.Key : New_Record_Key(records), Join_Map_Record(fields));
				}
				modified.insert(section);
			}
			for (int section : modified) scripts[section] = Write_Editor_Records(existing[section - 10]);
			return(true);
		}

		static int Group_Tag_Field(int section)
		{
			return(section == 11 ? 6 : section == 13 ? 8 : 7);
		}

		static bool Group_Has_Tag(std::string const & tag)
		{
			return(!tag.empty() && tag != "-1" && tag != "0" && !Equal_Section(tag, "None") && !Equal_Section(tag, "<none>"));
		}

		void Describe_Group()
		{
			GroupReferences = ClipboardObjects.empty() ? "" : std::to_string(ClipboardObjects.size()) + " objects";
			std::set<std::string> owners, tags;
			for (auto const & object : ClipboardObjects) {
				if (object.Section == 10) continue;
				owners.insert(object.Fields[0]);
				auto const & tag = object.Fields[Group_Tag_Field(object.Section)];
				if (Group_Has_Tag(tag)) tags.insert(tag);
			}
			for (auto const & owner : owners) GroupReferences += " · House " + owner;
			for (auto const & tag : tags) GroupReferences += " · Tag " + tag;
		}

		void Save_Group()
		{
			if (ClipboardObjects.empty() || Trim(GroupPath).empty()) {
				Status = "Copy an object region and enter a new group file path.";
				return;
			}
			std::string data = "OpenTS-Group-1 " + ClipboardTheater + ' '
				+ std::to_string(ClipboardSize.X) + ' ' + std::to_string(ClipboardSize.Y) + '\n';
			for (auto const & object : ClipboardObjects) {
				data += std::to_string(object.Section) + ' ' + std::to_string(object.Offset.X) + ' '
					+ std::to_string(object.Offset.Y) + ' ' + Join_Map_Record(object.Fields) + '\n';
			}
			if (data.size() > 2 * 1024 * 1024) {
				Status = "This object group exceeds the 2 MiB limit.";
				return;
			}
			try {
				auto const path = Trim(GroupPath);
				if (Write_New_Map_File(FS::path(std::u8string(path.begin(), path.end())), data, Status))
					Status = "Object group saved.";
			} catch (std::exception const &) {
				Status = "Enter a valid group file path.";
			}
		}

		void Open_Group()
		{
			Status = "Could not read a compatible object group.";
			try {
				auto const path = Trim(GroupPath);
				FS::path const input(std::u8string(path.begin(), path.end()));
				std::error_code error;
				if (!FS::is_regular_file(input, error) || FS::file_size(input, error) > 2 * 1024 * 1024 || error) return;
				std::ifstream file(input);
				std::string version, theater, line;
				int width, height;
				if (!(file >> version >> theater >> width >> height) || version != "OpenTS-Group-1"
					|| width < 1 || width > 512 || height < 1 || height > 512 || width * height > 16384) return;
				std::getline(file, line);
				if (!Trim(line).empty()) return;
				std::vector<RegionObject> objects;
				while (std::getline(file, line)) {
					std::istringstream row(line);
					int section, x, y;
					std::string value;
					if (!(row >> section >> x >> y) || !std::getline(row >> std::ws, value)
						|| section < 10 || section > 14 || x < 0 || x >= width || y < 0 || y >= height
						|| value.find_first_of(";\r\n") != value.npos) return;
					auto fields = Split_Map_Record(value);
					if (section == 10 ? fields.size() != 1 || fields[0].empty()
						: fields.size() <= static_cast<std::size_t>(Group_Tag_Field(section))
							|| fields[0].empty() || fields[1].empty()) return;
					objects.push_back({section, Point2D(x, y), std::move(fields), {}});
				}
				if (!file.eof() || objects.empty()) return;
				ClipboardObjects = std::move(objects);
				ClipboardSize = Point2D(width, height);
				ClipboardTheater = std::move(theater);
				Clipboard.assign(width * height, {});
				ClipboardTerrain = ClipboardOverlays = false;
				GroupSourceRevision.reset();
				Describe_Group();
				RegionPaste = true;
				RegionTarget.reset();
				PreviewChanged = true;
				Status = "Group loaded. Click the destination anchor.";
			} catch (std::exception const &) {}
		}

		void Load_Houses()
		{
			HouseChoices = Rules_Names("Houses");
			if (HouseChoices.empty()) {
				HouseFields.clear();
				HouseChanged = true;
				return;
			}
			auto picked = std::ranges::find(HouseChoices, HouseName);
			if (picked == HouseChoices.end()) HouseName = HouseChoices.front();
			Load_House_Fields();
		}

		void Load_House_Fields()
		{
			auto const data = Build_Copy();
			if (!data) return;
			BufferStraw input(data->data(), static_cast<int>(data->size()));
			INIClass ini;
			ini.Load(input);
			HouseFields.clear();
			LoadedHouse = HouseName;
			for (auto const & key : {"Credits", "Allies", "Color", "TechLevel", "IQ"}) {
				HouseFields.push_back({key, key, ini.Get_String(HouseName.c_str(), key)});
			}
			HouseChanged = true;
		}

		void Apply_House()
		{
			if (HouseName != LoadedHouse) {
				Status = "Load the selected house settings before applying.";
				return;
			}
			if (std::ranges::find(HouseChoices, HouseName) == HouseChoices.end() || !Safe_House_Section(HouseName)) return;
			auto const data = Build_Copy();
			if (!data) return;
			BufferStraw input(data->data(), static_cast<int>(data->size()));
			INIClass ini;
			ini.Load(input);
			EditorRecords rows;
			for (int i = 0; i < ini.Entry_Count(HouseName.c_str()); ++i) {
				auto const key = ini.Get_Entry(HouseName.c_str(), i);
				if (key) rows.emplace_back(key, ini.Get_String(HouseName.c_str(), key));
			}
			for (auto const & field : HouseFields) {
				auto const value = Trim(field.Value);
				int const number = Read_Number(value);
				if (value.find_first_of("\r\n;") != value.npos
					|| (!value.empty() && field.Key == "Credits" && (number < 0 || number > 1000000))
					|| (!value.empty() && field.Key == "TechLevel" && (number < 0 || number > 100))
					|| (!value.empty() && field.Key == "IQ" && (number < 0 || number > 100))) {
					Status = "Invalid house setting: " + field.Label + '.';
					return;
				}
				auto const row = std::ranges::find_if(rows, [&](auto const & record) {return(stricmp(record.first.c_str(), field.Key.c_str()) == 0);});
				if (value.empty()) { if (row != rows.end()) rows.erase(row); }
				else if (row == rows.end()) rows.emplace_back(field.Key, value);
				else row->second = value;
			}
			auto before = HouseEdits;
			HouseEdits.insert_or_assign(HouseName, Write_Editor_Records(rows));
			if (before != HouseEdits) {
				Remember([this, before = std::move(before), after = HouseEdits](bool forward) {
					HouseEdits = forward ? after : before;
					if (ToolTab == "houses") Load_House_Fields();
					Mark_Render_Pending();
				});
			}
			Status = "House settings applied. One credit unit equals 100 in-game credits.";
			Mark_Render_Pending();
		}

		bool Safe_House_Section(std::string const & name) const
		{
			if (name.empty() || name.find_first_of("[]=;\r\n") != name.npos) return(false);
			for (auto const & section : ScriptSections) if (Equal_Section(name, section.Key)) return(false);
			for (std::string_view reserved : {"basic", "houses", "waypoints", "isomappack5", "overlaypack", "overlaydatapack"}) {
				if (Equal_Section(name, reserved)) return(false);
			}
			return(true);
		}

		void Load_Bounds()
		{
			auto const data = Build_Copy();
			if (!data) return;
			BufferStraw input(data->data(), static_cast<int>(data->size()));
			INIClass ini;
			ini.Load(input);
			auto const size = ini.Get_Rect("Map", "Size", Rect(0, 0, 64, 64));
			auto const local = ini.Get_Rect("Map", "LocalSize", Rect(2, 4, size.Width - 4, size.Height - 10));
			BoundsWidth = std::to_string(size.Width); BoundsHeight = std::to_string(size.Height);
			BoundsX = std::to_string(local.X); BoundsY = std::to_string(local.Y);
			BoundsLocalWidth = std::to_string(local.Width); BoundsLocalHeight = std::to_string(local.Height);
			BoundsChanged = true;
			Preview_Bounds(false);
		}

		Point2D Bounds_To_Cell(int x, int y) const
		{
			int const doubled_y = y * 2;
			return(Point2D(((doubled_y + 1) >> 1) + x, (doubled_y >> 1) - x + BoundsPreviewWidth));
		}

		void Preview_Bounds(bool apply)
		{
			int const w = Read_Number(Trim(BoundsWidth)), h = Read_Number(Trim(BoundsHeight));
			int const x = Read_Number(Trim(BoundsX)), y = Read_Number(Trim(BoundsY));
			int const lw = Read_Number(Trim(BoundsLocalWidth)), lh = Read_Number(Trim(BoundsLocalHeight));
			if (w < 32 || w > 255 || h < 32 || h > 255 || x < 2 || y < 2 || x > 255 || y > 255
				|| lw < 1 || lw > w - x - 2 || lh < 1 || lh > h - y - 6 || w + h >= 512) {
				Status = "Use a 32–255 cell map and leave the engine’s playable margins.";
				return;
			}
			BoundsPreview = Rect(x, y, lw, lh);
			BoundsPreviewWidth = w;
			BoundsExcluded.clear();
			PreviewChanged = true;
			auto const data = Build_Copy();
			if (!data) return;
			BufferStraw input(data->data(), static_cast<int>(data->size()));
			INIClass ini;
			ini.Load(input);
			auto const previous = ini.Get_Rect("Map", "Size", Rect(0, 0, 64, 64));
			auto inside_map = [&](int cx, int cy) {
				return(cx >= 0 && cy >= 0 && cx < 512 && cy < 512 && std::abs(cx - cy) < w && cx + cy > w && cx + cy <= w + 2 * h);
			};
			int outside = 0;
			bool loses_records = false;
			auto count = [&](int cx, int cy) {
				if (cx < 0 || cx >= 512 || cy < 0 || cy >= 512) {
					loses_records = true;
					++outside;
					return;
				}
				if (!inside_map(cx, cy)) loses_records = true;
				int const sum = cx + cy;
				if (cx - cy >= 2 * (x + lw) - w || cy - cx >= w - 2 * x
					|| sum <= w + 2 * y || sum > w + 2 * (y + lh + 1)) {
					++outside;
					BoundsExcluded.emplace_back(cx, cy);
				}
			};
			for (auto const & row : Rows) count(row.X, row.Y);
			bool const modern = ini.Get_Int("Basic", "NewINIFormat", 0) >= 4;
			for (auto const name : {"Terrain", "Units", "Structures", "Infantry", "Aircraft", "CellTags", "Tubes"}) {
				for (int i = 0; i < ini.Entry_Count(name); ++i) {
					auto const key = ini.Get_Entry(name, i);
					if (!key) continue;
					if (std::string_view(name) == "Terrain" || std::string_view(name) == "CellTags") {
						int const cell = Read_Number(key);
						int const radix = std::string_view(name) == "CellTags" || modern ? 1000 : 128;
						if (cell >= 0) count(cell % radix, cell / radix);
						continue;
					}
					auto const fields = Split_Map_Record(ini.Get_String(name, key));
					if (std::string_view(name) == "Tubes") {
						if (fields.size() >= 5) { count(Read_Number(fields[0]), Read_Number(fields[1])); count(Read_Number(fields[3]), Read_Number(fields[4])); }
					} else if (fields.size() >= 5 && modern) count(Read_Number(fields[3]), Read_Number(fields[4]));
					else if (fields.size() >= 4) {
						int const cell = Read_Number(fields[3]);
						count(cell % 128, cell / 128);
					}
				}
			}
			if (w != previous.Width || h != previous.Height) {
				if (!OverlayWritable || !Preview.Terrain_Writable()) loses_records = true;
				for (int cy = 0; cy < 512; ++cy) {
					for (int cx = 0; cx < 512; ++cx) {
						int const cell = cy * 512 + cx;
						if (inside_map(cx, cy)) continue;
						if (Preview.Tile_State(cell).Record || (OverlayWritable && Overlay_At(cell).Type != 255)) loses_records = true;
					}
				}
				if (loses_records) {
					Status = "Resize would put saved cells outside the map or uses an unsupported layer. Enlarge or adjust the dimensions.";
					return;
				}
			}
			Status = std::to_string(outside) + " starts, objects, tags or endpoints outside the proposed playable area.";
			if (!apply) return;
			EditorRecords records;
			if (!Load_Records(16, records)) return;
			auto set = [&](std::string key, std::string value) {
				auto const it = std::ranges::find_if(records, [&](auto const & row) {return(stricmp(row.first.c_str(), key.c_str()) == 0);});
				if (it == records.end()) records.emplace_back(std::move(key), std::move(value));
				else it->second = std::move(value);
			};
			set("Size", "0,0," + std::to_string(w) + ',' + std::to_string(h));
			set("LocalSize", std::to_string(x) + ',' + std::to_string(y) + ',' + std::to_string(lw) + ',' + std::to_string(lh));
			Commit_Records(16, records);
			if (auto const updated = Build_Copy()) Preview.Load(*updated);
			Status = "Bounds applied. " + std::to_string(outside) + " starts, objects, tags or endpoints outside the playable area.";
		}

		void Commit_Records(int section, EditorRecords const & records)
		{
			auto before = ScriptEdits;
			ScriptEdits.insert_or_assign(section, Write_Editor_Records(records));
			Remember_Scripts(std::move(before));
			Mark_Render_Pending();
		}

		bool Load_Records(int section, EditorRecords & records)
		{
			Select_Script_Section(section);
			if (!ScriptAvailable) return(false);
			records = Read_Editor_Records(ScriptText);
			return(true);
		}

		void Load_Lighting()
		{
			EditorRecords records;
			if (!Load_Records(9, records)) return;
			Lights.clear();
			for (std::size_t i = 0; i < LightKeys.size(); ++i) {
				std::string key = (IonLighting ? "Ion" : "") + std::string(LightKeys[i]);
				auto find = [&](std::string_view name) {
					return(std::ranges::find_if(records, [&](auto const & record) { return(Equal_Section(record.first, name)); }));
				};
				auto value = find(key);
				if (value == records.end() && IonLighting) value = find(LightKeys[i]);
				Lights.push_back({LightKeys[i], std::move(key), value == records.end() ? LightDefaults[i] : value->second});
			}
			LightsChanged = true;
			Status = "Apply the lighting values to render the preview.";
		}

		void Apply_Lighting()
		{
			for (auto const & field : Lights) {
				auto const text = Trim(field.Value);
				double number = 0;
				std::istringstream stream{std::string(text)};
				stream.imbue(std::locale::classic());
				if (!(stream >> number) || !stream.eof() || !std::isfinite(number) || number < -10.0 || number > 10.0) {
					Status = "Enter a number from -10 to 10 for " + field.Label + '.';
					return;
				}
			}
			EditorRecords records;
			if (!Load_Records(9, records)) return;
			for (auto const & field : Lights) {
				auto const position = std::ranges::find_if(records, [&](auto const & record) { return(Equal_Section(record.first, field.Key)); });
				if (position == records.end()) records.emplace_back(field.Key, Trim(field.Value));
				else position->second = Trim(field.Value);
			}
			Commit_Records(9, records);
			Status = IonLighting ? "Ion-storm lighting applied." : "Normal lighting applied.";
		}

		void Use_Ion_Preview(std::string & source)
		{
			BufferStraw input(source.data(), static_cast<int>(source.size()));
			INIClass ini;
			ini.Load(input);
			std::string replacement = "[Lighting]" + Document.Newline;
			for (std::size_t i = 0; i < LightKeys.size(); ++i) {
				std::string const ion = "Ion" + std::string(LightKeys[i]);
				auto const normal = ini.Get_String("Lighting", LightKeys[i], LightDefaults[i]);
				replacement += std::string(LightKeys[i]) + '=' + ini.Get_String("Lighting", ion.c_str(), normal.c_str()) + Document.Newline;
			}
			Replace_Section(source, "lighting", replacement, Document.Newline);
		}

		bool Modern_Map() const
		{
			BufferStraw input(Document.Source.data(), static_cast<int>(Document.Source.size()));
			INIClass ini;
			ini.Load(input);
			return(ini.Get_Int("Basic", "NewINIFormat", 0) >= 4);
		}

		std::vector<std::string> Rules_Names(char const * list) const
		{
			std::vector<std::string> names;
			auto read = [&](INIClass const & ini) {
				for (int i = 0; i < ini.Entry_Count(list); ++i) {
					auto const key = ini.Get_Entry(list, i);
					if (!key) continue;
					auto value = ini.Get_String(list, key);
					if (!value.empty() && std::ranges::find(names, value) == names.end()) names.push_back(std::move(value));
				}
			};
			for (auto name : {"RULES.INI", "FIRESTRM.INI"}) {
				if (name[0] == 'F' && !Addon_Enabled(ADDON_FIRESTORM)) continue;
				INIClass ini;
				CCFileClass file(name);
				if (ini.Load(file) > 0) read(ini);
			}
			BufferStraw input(Document.Source.data(), static_cast<int>(Document.Source.size()));
			INIClass ini;
			ini.Load(input);
			read(ini);
			std::sort(names.begin(), names.end());
			return(names);
		}

		void Load_Actor_Choices()
		{
			ActorTypes = Rules_Names(ActorFormats[ActorCategory].Types);
			ActorHouses = Rules_Names("Houses");
			for (int i = 1; i <= 8; ++i) ActorHouses.push_back("Spawn" + std::to_string(i));
			if (!ActorTypes.empty() && std::ranges::find(ActorTypes, ActorType) == ActorTypes.end()) ActorType = ActorTypes.front();
			if (!ActorHouses.empty() && std::ranges::find(ActorHouses, ActorOwner) == ActorHouses.end()) ActorOwner = ActorHouses.front();
			ActorFieldsChanged = true;
		}

		bool Actor_Choice(std::vector<std::string> const & choices, std::string const & token) const
		{
			return(std::ranges::any_of(choices, [&](auto const & choice) { return(stricmp(choice.c_str(), token.c_str()) == 0); }));
		}

		void Load_Cell_Tags()
		{
			EditorRecords records;
			if (!Load_Records(3, records)) return;
			TagChoices.clear();
			for (auto const & record : records) {
				if (record.first.empty()) continue;
				auto const fields = Split_Map_Record(record.second);
				TagChoices.push_back({record.first, fields.size() > 1 ? fields[1] + " (" + record.first + ')' : record.first});
			}
			if (std::ranges::find(TagChoices, CellTag, &EditorTagChoice::Id) == TagChoices.end()) {
				CellTag = TagChoices.empty() ? "" : TagChoices.front().Id;
			}
			TagChoicesChanged = true;
			if (!Load_Records(4, records)) return;
			TagCells.clear();
			TagRadix = Modern_Map() ? 1000 : 128;
			int const radix = TagRadix;
			for (auto const & record : records) {
				int const cell = Read_Number(record.first);
				if (cell < 0 || !Preview.Inside(cell % radix, cell / radix)) continue;
				TagCells.emplace(cell % radix + 1000 * (cell / radix), record.second);
			}
			PreviewChanged = true;
			Status = TagChoices.empty() ? "Create a tag in Scripts before painting." : "Choose a tag and paint cells.";
		}

		void Paint_Cell_Tag(int x, int y)
		{
			int const radix = TagRadix;
			if (x >= radix || (!TagErase && std::ranges::find(TagChoices, CellTag, &EditorTagChoice::Id) == TagChoices.end())) {
				Status = "Select an existing tag and a cell supported by the map format.";
				return;
			}
			auto const current = TagCells.find(y * 1000 + x);
			if (TagErase ? current == TagCells.end() : current != TagCells.end() && current->second == CellTag) return;
			if (!TagStroke) {
				EditorRecords records;
				if (!Load_Records(4, records)) return;
				TagStroke = std::move(records);
			}
			int const key = y * radix + x;
			auto const position = std::ranges::find_if(*TagStroke, [key](auto const & row) {return(Read_Number(row.first) == key);});
			if (TagErase) {
				if (position != TagStroke->end()) TagStroke->erase(position);
				TagCells.erase(y * 1000 + x);
			} else {
				if (position == TagStroke->end()) TagStroke->emplace_back(std::to_string(key), CellTag);
				else position->second = CellTag;
				TagCells.insert_or_assign(y * 1000 + x, CellTag);
			}
			PreviewChanged = true;
		}

		void Open_Tag_Trigger()
		{
			EditorRecords records;
			if (!Load_Records(3, records)) return;
			auto const tag = std::ranges::find(records, CellTag, &EditorRecords::value_type::first);
			if (tag == records.end()) return;
			auto const fields = Split_Map_Record(tag->second);
			if (fields.size() < 3) {
				Status = "This tag has no trigger reference.";
				return;
			}
			Scripts = true;
			Select_Script_Section(0);
			VisualScript = TriggerInspector = false;
			TriggerToFocus = fields[2];
			Status = "Trigger " + fields[2] + " for " + fields[1] + '.';
		}

		Point2D Structure_Footprint(std::string const & type) const
		{
			auto const cached = FoundationSizes.find(type);
			if (cached != FoundationSizes.end()) return(cached->second);
			INIClass art;
			CCFileClass base("ART.INI");
			art.Load(base);
			INIClass expansion;
			if (Addon_Enabled(ADDON_FIRESTORM)) {
				CCFileClass extra("ARTFS.INI");
				expansion.Load(extra);
			}
			auto const size = expansion.Get_String(type.c_str(), "Foundation", art.Get_String(type.c_str(), "Foundation", "1x1").c_str());
			auto const split = size.find_first_of("xX");
			Point2D footprint(1, 1);
			if (split != size.npos) {
				footprint = Point2D(std::clamp(Read_Number(std::string_view(size).substr(0, split)), 1, 16),
					std::clamp(Read_Number(std::string_view(size).substr(split + 1)), 1, 16));
			}
			FoundationSizes.emplace(type, footprint);
			return(footprint);
		}

		bool Refresh_Placed_Actors()
		{
			if (PlacedActorsRevision == Revision) return(true);
			auto const draft = Build_Copy();
			if (!draft) return(false);
			BufferStraw input(draft->data(), static_cast<int>(draft->size()));
			INIClass ini;
			ini.Load(input);
			PlacedActors.clear();
			for (int section = 10; section <= 14; ++section) {
				auto const name = ScriptSections[section].Name;
				for (int i = 0; i < ini.Entry_Count(name); ++i) {
					auto const key = ini.Get_Entry(name, i);
					if (!key) continue;
					auto const fields = Split_Map_Record(ini.Get_String(name, key));
					if (section != 10 && fields.size() < 6) continue;
					int const cell = Read_Number(key);
					int const x = section == 10 ? cell % 1000 : Read_Number(fields[3]);
					int const y = section == 10 ? cell / 1000 : Read_Number(fields[4]);
					if (!Preview.Inside(x, y)) continue;
					auto const size = section == 11 ? Structure_Footprint(fields[1]) : Point2D(1, 1);
					PlacedActors.push_back({section, key, Point2D(x, y), size, section == 13 ? Read_Number(fields[5]) : -1});
				}
			}
			PlacedActorsRevision = Revision;
			return(true);
		}

		unsigned Actor_Blocked_Subcells(Point2D target, std::string const & ignore, std::set<int> * blocked_cells = nullptr)
		{
			int const section = ActorFormats[ActorCategory].Section;
			auto const size = ActorCategory == 1 ? DragFootprint : Point2D(1, 1);
			unsigned blocked = 0;
			for (int dy = 0; dy < size.Y; ++dy) for (int dx = 0; dx < size.X; ++dx) {
				int const x = target.X + dx, y = target.Y + dy;
				if (Preview.Inside(x, y)) continue;
				if (!blocked_cells) return(31);
				blocked = 31;
				blocked_cells->insert(y * 1000 + x);
			}
			if (!Refresh_Placed_Actors()) return(31);
			for (auto const & actor : PlacedActors) {
				if (actor.Section == section && actor.Key == ignore) continue;
				int const x = actor.Anchor.X, y = actor.Anchor.Y;
				if (target.X >= x + actor.Size.X || x >= target.X + size.X
					|| target.Y >= y + actor.Size.Y || y >= target.Y + size.Y) continue;
				if (actor.Section == 13 && section == 13 && actor.Spot >= 0 && actor.Spot < 5) {
					blocked |= 1u << actor.Spot;
					if (!blocked_cells || Read_Number(ActorSpot) != actor.Spot) continue;
				} else {
					if (!blocked_cells) return(31);
					blocked = 31;
				}
				if (blocked_cells) {
					for (int by = std::max(y, target.Y); by < std::min(y + actor.Size.Y, target.Y + size.Y); ++by)
						for (int bx = std::max(x, target.X); bx < std::min(x + actor.Size.X, target.X + size.X); ++bx)
							blocked_cells->insert(by * 1000 + bx);
				}
			}
			return(blocked);
		}

		bool Actor_Target_Free(Point2D target, int spot, std::string const & ignore)
		{
			unsigned const blocked = Actor_Blocked_Subcells(target, ignore);
			return(spot >= 0 && spot < 5 ? !(blocked & (1u << spot)) : blocked == 0);
		}

		void Describe_Subcells(Point2D target)
		{
			auto const blocked = Actor_Blocked_Subcells(target, "");
			ActorSpotStatus = blocked == 31 ? "No free subcells." : "Free subcells:";
			for (int spot = 0; spot < 5; ++spot) if (!(blocked & (1u << spot))) ActorSpotStatus += ' ' + std::to_string(spot);
		}

		void Move_Actor(Point2D target, bool duplicate)
		{
			DragPreview = Point2D(-1, -1);
			PreviewChanged = true;
			if (ActorKey.empty() || target == ActorCell) return;
			auto const section = ActorFormats[ActorCategory].Section;
			EditorRecords records;
			if (!Load_Records(section, records)) return;
			auto position = std::ranges::find(records, ActorKey, &EditorRecords::value_type::first);
			if (position == records.end()) return;
			auto fields = Split_Map_Record(position->second);
			if (fields.size() < 6) return;
			int const spot = ActorCategory == 2 ? Read_Number(fields[5]) : -1;
			if (ActorCategory == 2 && (spot < 0 || spot > 4)) { Status = "The source infantry has an invalid subcell."; return; }
			if (!Actor_Target_Free(target, spot, duplicate ? "" : ActorKey)) {
				Status = "The destination footprint is blocked.";
				return;
			}
			fields[3] = std::to_string(target.X); fields[4] = std::to_string(target.Y);
			if (duplicate) {
				if (ActorCategory == 0 && fields.size() > 11) fields[11] = "-1";
				ActorKey = New_Record_Key(records);
				records.emplace_back(ActorKey, Join_Map_Record(fields));
			} else position->second = Join_Map_Record(fields);
			ActorCell = target;
			Commit_Records(section, records);
			if (ActorCategory == 2) Describe_Subcells(target);
			Status = duplicate ? "Object duplicated." : "Object moved.";
		}

		void Show_Balance()
		{
			auto const draft = Build_Copy();
			if (!draft) return;
			BufferStraw input(draft->data(), static_cast<int>(draft->size()));
			INIClass map, rules, expansion;
			map.Load(input);
			CCFileClass rules_file("RULES.INI");
			rules.Load(rules_file);
			if (Addon_Enabled(ADDON_FIRESTORM)) {
				CCFileClass file("FIRESTRM.INI");
				expansion.Load(file);
			}
			std::map<std::string, std::pair<long long, long long>> power;
			for (int i = 0; i < map.Entry_Count("Structures"); ++i) {
				auto const key = map.Get_Entry("Structures", i);
				if (!key) continue;
				auto const fields = Split_Map_Record(map.Get_String("Structures", key));
				if (fields.size() < 2) continue;
				char const * type = fields[1].c_str();
				long long const value = map.Get_Int(type, "Power", expansion.Get_Int(type, "Power", rules.Get_Int(type, "Power", 0)));
				auto & [generation, demand] = power[fields[0]];
				if (value > 0) generation += value;
				else demand -= value;
			}
			BalanceText = "Power by house (generation / demand)\n";
			if (power.empty()) BalanceText += "No placed structures.\n";
			for (auto const & [name, values] : power) {
				BalanceText += name + ": " + std::to_string(values.first) + " / " + std::to_string(values.second);
				if (values.first < values.second) BalanceText += " (shortfall)";
				BalanceText += '\n';
			}
			BalanceText += "\nEstimated Tiberium within 12 cells of each start\n";
			if (!OverlayWritable) BalanceText += "Overlay data is unavailable.\n";
			else {
				bool found = false;
				for (auto const & row : Rows) {
					if (row.Slot >= 8 || !Preview.Inside(row.X, row.Y)) continue;
					found = true;
					long long credits = 0;
					for (int dy = -12; dy <= 12; ++dy) {
						for (int dx = -12; dx <= 12; ++dx) {
							if (dx * dx + dy * dy > 144 || !Preview.Inside(row.X + dx, row.Y + dy)) continue;
							auto const cell = Overlay_At((row.Y + dy) * 512 + row.X + dx);
							for (std::size_t type = 0; type < ResourceOverlay.size(); ++type) {
								int const base = ResourceOverlay[type];
								if (cell.Type >= base && cell.Type < base + (base == OVERLAY_LARGE_TIBERIUM01 ? 12 : 20)) {
									credits += 1LL * ResourceValue[type] * (cell.Data + 1);
									break;
								}
							}
						}
					}
					BalanceText += "P" + std::to_string(row.Slot + 1) + ": " + std::to_string(credits) + " credits\n";
				}
				if (!found) BalanceText += "No player starts.\n";
			}
			BalanceOpen = true;
		}

		void Actor_At(int x, int y)
		{
			auto const & format = ActorFormats[ActorCategory];
			if (!Modern_Map()) {
				Status = "Unit and structure editing requires the current scenario format.";
				return;
			}
			DragFootprint = ActorCategory == 1 ? Structure_Footprint(ActorType) : Point2D(1, 1);
			int const section = format.Section;
			EditorRecords records;
			if (!Load_Records(section, records)) return;
			if (ActorCategory == 2) Describe_Subcells(Point2D(x, y));
			auto const hit = std::ranges::find_if(records, [&](auto const & record) {
				auto const fields = Split_Map_Record(record.second);
				if (fields.size() < static_cast<std::size_t>(format.Tag + 1)) return(false);
				int const ox = Read_Number(fields[3]), oy = Read_Number(fields[4]);
				auto const size = ActorCategory == 1 ? Structure_Footprint(fields[1]) : Point2D(1, 1);
				return(x >= ox && y >= oy && x < ox + size.X && y < oy + size.Y
					&& (ActorCategory != 2 || fields[5] == ActorSpot));
			});
			PreviewChanged |= !ActorKey.empty();
			ActorKey.clear();
			if (ActorInspect) {
				if (hit == records.end()) {
					Status = "No matching object at this cell.";
					return;
				}
				auto const fields = Split_Map_Record(hit->second);
				ActorKey = hit->first;
				ActorCell = Point2D(Read_Number(fields[3]), Read_Number(fields[4]));
				PreviewChanged = true;
				ActorType = fields[1];
				DragFootprint = ActorCategory == 1 ? Structure_Footprint(ActorType) : Point2D(1, 1);
				ActorOwner = fields[0]; ActorHealth = fields[2];
				ActorFacing = fields[format.Facing]; ActorTag = fields[format.Tag];
				if (ActorCategory == 2) ActorSpot = fields[5];
				if (!Actor_Choice(ActorTypes, ActorType)) ActorTypes.push_back(ActorType);
				if (!Actor_Choice(ActorHouses, ActorOwner)) ActorHouses.push_back(ActorOwner);
				ActorFieldsChanged = true;
				Status = "Editing " + ActorType + " at " + std::to_string(x) + ", " + std::to_string(y) + '.';
			} else if (hit != records.end()) {
				Status = "This cell already has an object of this category.";
			} else {
				Edit_Actor(false, Point2D(x, y));
			}
		}

		void Edit_Actor(bool remove, std::optional<Point2D> placement = std::nullopt)
		{
			auto const & format = ActorFormats[ActorCategory];
			if ((!placement && ActorKey.empty()) || !Modern_Map()) return;
			int const section = format.Section;
			EditorRecords records;
			if (!Load_Records(section, records)) return;
			auto position = std::ranges::find(records, ActorKey, &EditorRecords::value_type::first);
			if (!placement && position == records.end()) return;
			if (remove) {
				int const removed = static_cast<int>(std::count_if(records.begin(), position, [](auto const & record) {
					return(!record.first.empty());
				}));
				records.erase(position);
				if (ActorCategory == 0) {
					for (auto & record : records) {
						if (record.first.empty()) continue;
						auto fields = Split_Map_Record(record.second);
						if (fields.size() <= 11) continue;
						int const follower = Read_Number(fields[11]);
						if (follower < removed) continue;
						fields[11] = std::to_string(follower == removed ? -1 : follower - 1);
						record.second = Join_Map_Record(fields);
					}
				}
				ActorKey.clear();
				PreviewChanged = true;
				Commit_Records(section, records);
				Status = "Object removed.";
				return;
			}
			auto token = [](std::string const & text) {
				return(!Trim(text).empty() && text.size() < 32 && text.find_first_of(",;=\r\n") == std::string::npos);
			};
			int const health = Read_Number(Trim(ActorHealth)), facing = Read_Number(Trim(ActorFacing));
			if (!token(ActorOwner) || !token(ActorType) || !token(ActorTag)
				|| health < 1 || health > 256 || facing < 0 || facing > 255) {
				Status = "Enter an owner, type and tag, health 1–256, and facing 0–255.";
				return;
			}
			std::vector<std::string> fields;
			if (placement) {
				if (!Actor_Choice(ActorTypes, std::string(Trim(ActorType)))) {
					Status = "That type ID is absent from the map and game rules.";
					return;
				}
				fields = {"", "", "", std::to_string(placement->X), std::to_string(placement->Y), ""};
				fields.resize(format.Tag + 1);
				if (ActorCategory != 1) fields[6] = "Guard";
			} else {
				fields = Split_Map_Record(position->second);
				if (fields.size() < static_cast<std::size_t>(format.Tag + 1)) return;
				if (Trim(ActorType) != fields[1] && !Actor_Choice(ActorTypes, std::string(Trim(ActorType)))) {
					Status = "That type ID is absent from the map and game rules.";
					return;
				}
			}
			if (Trim(ActorOwner) != fields[0] && !Actor_Choice(ActorHouses, std::string(Trim(ActorOwner)))) {
				Status = "Choose an owner from the map or game rules.";
				return;
			}
			fields[0] = Trim(ActorOwner); fields[1] = Trim(ActorType); fields[2] = std::to_string(health);
			fields[format.Facing] = std::to_string(facing); fields[format.Tag] = Trim(ActorTag);
			int const spot = Read_Number(Trim(ActorSpot));
			if (ActorCategory == 2) {
				if (spot < 0 || spot > 4) { Status = "Choose infantry subcell 0–4."; return; }
				fields[5] = std::to_string(spot);
			}
			Point2D const target(Read_Number(fields[3]), Read_Number(fields[4]));
			DragFootprint = ActorCategory == 1 ? Structure_Footprint(ActorType) : Point2D(1, 1);
			if (!Actor_Target_Free(target, ActorCategory == 2 ? spot : -1, placement ? "" : ActorKey)) {
				Status = "The object footprint or infantry subcell is occupied or outside the map.";
				return;
			}
			if (placement) records.emplace_back(New_Record_Key(records), Join_Map_Record(fields));
			else position->second = Join_Map_Record(fields);
			Commit_Records(section, records);
			if (ActorCategory == 2) Describe_Subcells(target);
			Status = placement ? "Object placed." : "Object properties updated.";
		}

		void Load_Tunnel(int x, int y)
		{
			EditorRecords records;
			if (!Load_Records(15, records)) return;
			if (TunnelRoute.empty()) {
				TunnelKey.clear();
				for (auto const & [key, value] : records) {
					auto const fields = Split_Map_Record(value);
					if (fields.size() < 6) continue;
					Point2D const start(Read_Number(fields[0]), Read_Number(fields[1]));
					Point2D const end(Read_Number(fields[3]), Read_Number(fields[4]));
					if (!Preview.Inside(start.X, start.Y) || !Preview.Inside(end.X, end.Y)) continue;
					if (ForceNewTunnel || (Point2D(x, y) != start && Point2D(x, y) != end)) continue;
					std::vector<Point2D> route{start};
					for (std::size_t i = 5; i < fields.size() && i < 104; ++i) {
						int const dir = Read_Number(fields[i]);
						if (dir < 0 || dir >= static_cast<int>(TunnelSteps.size())) break;
						auto const & step = TunnelSteps[dir];
						auto const point = Point2D(route.back().X + step[0], route.back().Y + step[1]);
						if (!Preview.Inside(point.X, point.Y)) break;
						route.push_back(point);
					}
					TunnelRoute = std::move(route);
					TunnelKey = key;
					PreviewChanged = true;
					Status = "Tunnel selected. Use Trim or extend the exit, then Apply.";
					return;
				}
				ForceNewTunnel = false;
				TunnelRoute.emplace_back(x, y);
			} else {
				auto const start = TunnelRoute.back();
				int const dx = x - start.X, dy = y - start.Y;
				int const distance = std::max(std::abs(dx), std::abs(dy));
				if (TunnelRoute.size() + distance > 100) {
					Status = "A tunnel can have at most 99 steps.";
					return;
				}
				for (int i = 1; i <= distance; ++i) {
					int const px = start.X + static_cast<int>(std::lround(static_cast<double>(i * dx) / distance));
					int const py = start.Y + static_cast<int>(std::lround(static_cast<double>(i * dy) / distance));
					if (!Preview.Inside(px, py)) {
						Status = "Keep the tunnel route inside the map.";
						return;
					}
				}
				for (int i = 1; i <= distance; ++i) {
					TunnelRoute.emplace_back(start.X + static_cast<int>(std::lround(static_cast<double>(i * dx) / distance)),
						start.Y + static_cast<int>(std::lround(static_cast<double>(i * dy) / distance)));
				}
			}
			PreviewChanged = true;
			Status = "Click to extend the route. Apply to save it.";
		}

		void Apply_Tunnel(bool remove)
		{
			if (remove ? TunnelKey.empty() : TunnelRoute.size() < 2) return;
			EditorRecords records;
			if (!Load_Records(15, records)) return;
			auto position = std::ranges::find(records, TunnelKey, &EditorRecords::value_type::first);
			if (remove) {
				if (position == records.end()) return;
				records.erase(position);
			} else {
				auto const & start = TunnelRoute.front();
				auto const & end = TunnelRoute.back();
				for (auto const & record : records) {
					if (record.first == TunnelKey) continue;
					auto const fields = Split_Map_Record(record.second);
					if (fields.size() >= 2 && Read_Number(fields[0]) == start.X && Read_Number(fields[1]) == start.Y) {
						Status = "A tunnel already uses that entrance.";
						return;
					}
				}
				if (start == end) {
					Status = "The tunnel entrance and exit must differ.";
					return;
				}
				std::vector<std::string> directions;
				for (std::size_t i = 1; i < TunnelRoute.size(); ++i) {
					auto const step = std::array{TunnelRoute[i].X - TunnelRoute[i - 1].X, TunnelRoute[i].Y - TunnelRoute[i - 1].Y};
					auto const facing = std::ranges::find(TunnelSteps, step);
					if (facing == TunnelSteps.end()) return;
					directions.push_back(std::to_string(facing - TunnelSteps.begin()));
				}
				std::vector<std::string> fields{std::to_string(start.X), std::to_string(start.Y), directions.front(),
					std::to_string(end.X), std::to_string(end.Y)};
				fields.insert(fields.end(), directions.begin(), directions.end());
				fields.emplace_back("-1");
				if (position == records.end()) {
					TunnelKey = New_Record_Key(records);
					records.emplace_back(TunnelKey, Join_Map_Record(fields));
				} else position->second = Join_Map_Record(fields);
			}
			Commit_Records(15, records);
			if (remove) {
				TunnelRoute.clear();
				TunnelKey.clear();
			}
			PreviewChanged = true;
			Status = remove ? "Tunnel removed." : "Tunnel route applied. Place the matching entrance and exit tiles separately.";
		}

		void Clear_Object_Selection()
		{
			PreviewChanged |= SelectedObjectCell >= 0;
			SelectedObjectCell = -1;
			SelectedObjectName.clear();
			MovingObject = false;
		}

		void Edit_Object(int target, bool remove)
		{
			auto const draft = Build_Copy();
			if (!draft) return;
			BufferStraw input(draft->data(), static_cast<int>(draft->size()));
			INIClass ini;
			ini.Load(input);
			if (ini.Get_Int("Basic", "NewINIFormat", 0) < 4) {
				Status = "Editing scenery requires a map in the current scenario format.";
				return;
			}
			bool const moving = MovingObject && !remove;
			if (!moving && !remove) {
				char name[256];
				std::string const key = std::to_string(target);
				Clear_Object_Selection();
				if (!ini.Get_String("Terrain", key.c_str(), "", name, sizeof(name))) {
					Status = "No scenery object at this cell.";
					return;
				}
				SelectedObjectCell = target;
				SelectedObjectName = name;
				PreviewChanged = true;
				Status = "Selected " + SelectedObjectName + '.';
				return;
			}

			if (moving && target == SelectedObjectCell) {
				MovingObject = false;
				Status = "Move cancelled.";
				return;
			}
			if (moving && ini.Is_Present("Terrain", std::to_string(target).c_str())) {
				Status = "That cell already contains a scenery object.";
				return;
			}

			constexpr int terrain_section = 10;
			Select_Script_Section(terrain_section);
			if (!ScriptAvailable) return;
			std::string updated;
			bool found = false;
			for (std::string_view lines = ScriptText; !lines.empty();) {
				auto const end = lines.find('\n');
				auto const line = lines.substr(0, end);
				auto const equals = line.find('=');
				if (equals != line.npos && Read_Number(Trim(line.substr(0, equals))) == SelectedObjectCell) {
					found = true;
					if (moving) updated += std::to_string(target) + std::string(line.substr(equals)) + '\n';
				} else {
					updated.append(line);
					updated += '\n';
				}
				if (end == lines.npos) break;
				lines.remove_prefix(end + 1);
			}
			if (!found) {
				Clear_Object_Selection();
				Status = "The scenery object is no longer at that cell.";
				return;
			}
			auto before = ScriptEdits;
			ScriptEdits.insert_or_assign(terrain_section, std::move(updated));
			Remember_Scripts(std::move(before));
			if (auto const revised = Build_Copy()) Preview.Load(*revised);
			if (remove) Clear_Object_Selection();
			else {
				SelectedObjectCell = target;
				MovingObject = false;
			}
			PreviewChanged = true;
			Mark_Render_Pending();
			Status = remove ? "Scenery removed." : "Scenery moved.";
		}

		void Place_Object(int x, int y, EditorAsset const & asset)
		{
			auto const draft = Build_Copy();
			if (!draft) return;
			BufferStraw input(draft->data(), static_cast<int>(draft->size()));
			INIClass ini;
			ini.Load(input);
			if (ini.Get_Int("Basic", "NewINIFormat", 0) < 4) {
				Status = "Object placement requires a map in the current scenario format.";
				return;
			}
			std::string const key = std::to_string(y * 1000 + x);
			if (ini.Is_Present("Terrain", key.c_str())) {
				Status = "A terrain object is already on this cell.";
				return;
			}
			constexpr int terrain_section = 10;
			Select_Script_Section(terrain_section);
			if (!ScriptAvailable) return;
			auto before = ScriptEdits;
			ScriptEdits[terrain_section] = ScriptText + key + "=" + asset.Object + "\n";
			Remember_Scripts(std::move(before));
			if (auto const updated = Build_Copy()) Preview.Load(*updated);
			PreviewChanged = true;
			Mark_Render_Pending();
			Status = asset.Name + " placed.";
		}

		void Find_Triggers()
		{
			EditorRecords records;
			if (!Load_Records(0, records)) return;
			TriggerChoices.clear();
			auto upper = [](std::string text) {
				for (char & c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				return(text);
			};
			auto const search = upper(TriggerSearch);
			for (auto const & [key, value] : records) {
				if (key.empty()) continue;
				auto const fields = Split_Map_Record(value);
				std::string label = fields.size() >= 3 ? fields[2] + " · " + key : key;
				if (upper(label).find(search) != std::string::npos) TriggerChoices.push_back({key, std::move(label)});
			}
			Load_Inspected_Trigger();
		}

		bool Trigger_Steps(bool action, EditorRecords & records, std::vector<std::string> & fields)
		{
			if (InspectedTrigger.empty() || !Load_Records(action ? 2 : 1, records)) return(false);
			auto const row = std::ranges::find(records, InspectedTrigger, &EditorRecords::value_type::first);
			if (row == records.end()) return(false);
			fields = Split_Map_Record(row->second);
			int const width = action ? 8 : 3;
			int const count = fields.empty() ? -1 : Read_Number(fields.front());
			if (count < 0 || fields.size() != 1 + static_cast<std::size_t>(count) * width) {
				Status = "This record needs repair in Raw Records.";
				return(false);
			}
			return(true);
		}

		void Load_Inspected_Trigger()
		{
			SelectedStep = -1;
			TriggerEvents.clear();
			TriggerActions.clear();
			TriggerLinks.clear();
			for (bool action : {false, true}) {
				EditorRecords records;
				std::vector<std::string> fields;
				if (!Trigger_Steps(action, records, fields)) continue;
				std::size_t const width = action ? 8 : 3;
				auto & choices = action ? TriggerActions : TriggerEvents;
				for (std::size_t i = 1; i < fields.size(); i += width) {
					auto label = Script_Step_Name(action, Read_Number(fields[i])) + " · " + fields[i + 2];
					if (action && fields[i + 7] != "0") label += " · Waypoint " + fields[i + 7];
					choices.push_back({std::to_string(choices.size()), std::move(label)});
				}
			}
			EditorRecords tags;
			if (!InspectedTrigger.empty() && Load_Records(3, tags)) {
				for (auto const & [key, value] : tags) {
					auto const fields = Split_Map_Record(value);
					if (fields.size() >= 3 && Equal_Section(fields[2], InspectedTrigger))
						TriggerLinks += (TriggerLinks.empty() ? "Tags: " : ", ") + fields[1] + " (" + key + ')';
				}
			}
		}

		void Select_Trigger_Step(bool action, int step)
		{
			EditorRecords records;
			std::vector<std::string> fields;
			if (step < 0 || !Trigger_Steps(action, records, fields) || step >= Read_Number(fields.front())) return;
			std::size_t const i = 1 + step * (action ? 8 : 3);
			SelectedStep = step;
			SelectedStepAction = action;
			StepType = fields[i];
			StepCode = fields[i + 1];
			StepValue = fields[i + 2];
			StepWaypoint = action ? fields[i + 7] : "";
		}

		void Edit_Trigger_Step(std::string const & operation)
		{
			bool const action = SelectedStepAction;
			EditorRecords records;
			std::vector<std::string> fields;
			int const step = SelectedStep;
			if (step < 0 || !Trigger_Steps(action, records, fields) || step >= Read_Number(fields.front())) return;
			int const width = action ? 8 : 3;
			int const i = 1 + step * width;
			int selected = step;
			if (operation == "step-apply") {
				int const type = Read_Number(Trim(StepType)), code = Read_Number(Trim(StepCode));
				auto const valid = [](std::string_view value) { return !value.empty() && value.find_first_of(",;\r\n") == value.npos; };
				if (type < 0 || type >= (action ? int(TACTION_COUNT) : int(TEVENT_COUNT)) || code < 0
					|| code > (action ? 4 : 1) || !valid(Trim(StepValue)) || (action && !valid(Trim(StepWaypoint)))) {
					Status = "Enter a valid type, parameter code, value, and waypoint.";
					return;
				}
				fields[i] = std::to_string(type);
				fields[i + 1] = std::to_string(code);
				fields[i + 2] = Trim(StepValue);
				if (action) fields[i + 7] = Trim(StepWaypoint);
			} else {
				selected += operation == "step-up" ? -1 : 1;
				if (selected < 0 || selected >= Read_Number(fields.front())) return;
				std::swap_ranges(fields.begin() + i, fields.begin() + i + width, fields.begin() + 1 + selected * width);
			}
			auto const row = std::ranges::find(records, InspectedTrigger, &EditorRecords::value_type::first);
			row->second = Join_Map_Record(fields);
			Commit_Records(action ? 2 : 1, records);
			Load_Inspected_Trigger();
			Select_Trigger_Step(action, selected);
			Status = "Trigger step updated.";
		}

		void Apply_Visual_Script()
		{
			auto const draft = Build_Copy();
			if (!draft) return;
			BufferStraw source(draft->data(), static_cast<int>(draft->size()));
			INIClass ini;
			ini.Load(source);
			auto const name = Trim(VisualName);
			auto const house = Trim(VisualHouse);
			auto const valid_field = [](std::string_view value) {
				return(!value.empty() && value.size() <= 63 && value.find_first_of(",=;[]\r\n") == value.npos);
			};
			if (!valid_field(name) || !valid_field(house)) {
				Status = "Enter a name and house of up to 63 characters without INI punctuation.";
				return;
			}

			int const waypoint = Read_Number(Trim(VisualWaypoint));
			bool const has_waypoint = std::ranges::find(Rows, waypoint, &WaypointRow::Slot) != Rows.end();
			auto const listed = [&](char const * section, std::string const & id) {
				for (int i = 0; i < ini.Entry_Count(section); ++i) {
					auto const entry = ini.Get_Entry(section, i);
					if (entry && Equal_Section(ini.Get_String(section, entry), id)) return(true);
				}
				return(false);
			};
			std::string key;
			for (unsigned id = 0x7E000000; id < 0x7E010000; ++id) {
				char candidate[9];
				std::snprintf(candidate, sizeof(candidate), "%08X", id);
				if (!ini.Is_Present(candidate) && !ini.Is_Present("Triggers", candidate)
					&& !ini.Is_Present("Tags", candidate) && !ini.Is_Present("Events", candidate)
					&& !ini.Is_Present("Actions", candidate)) {
					key = candidate;
					break;
				}
			}
			if (key.empty()) {
				Status = "No free scenario record ID is available.";
				return;
			}

			if (VisualKind == "team") {
				if (!has_waypoint || !listed("TaskForces", VisualForce) || !listed("ScriptTypes", VisualTeamScript)) {
					Status = "Select an existing waypoint and enter listed Task Force and Script IDs.";
					return;
				}
				EditorRecords teams;
				if (!Load_Records(5, teams)) return;
				if (!VisualTeam.empty()) {
					if (!listed("TeamTypes", VisualTeam) || !ini.Is_Present(VisualTeam.c_str())) {
						Status = "The team ID must identify a listed TeamTypes section.";
						return;
					}
					key = VisualTeam;
				} else teams.emplace_back(New_Record_Key(teams), key);

				EditorRecords properties;
				for (int i = 0; i < ini.Entry_Count(key.c_str()); ++i) {
					auto const field = ini.Get_Entry(key.c_str(), i);
					if (field) properties.emplace_back(field, ini.Get_String(key.c_str(), field));
				}
				for (auto const & [field, value] : EditorRecords{{"Name", std::string(name)}, {"House", std::string(house)},
					{"Waypoint", Waypoint_To_Name(waypoint)}, {"TaskForce", VisualForce}, {"Script", VisualTeamScript}}) {
					auto const position = std::ranges::find_if(properties, [&](auto const & item) { return Equal_Section(item.first, field); });
					if (position == properties.end()) properties.emplace_back(field, value);
					else position->second = value;
				}
				auto scripts = ScriptEdits;
				auto sections = HouseEdits;
				ScriptEdits[5] = Write_Editor_Records(teams);
				HouseEdits[key] = Write_Editor_Records(properties);
				Remember([this, scripts = std::move(scripts), sections = std::move(sections),
					after_scripts = ScriptEdits, after_sections = HouseEdits](bool forward) {
					ScriptEdits = forward ? after_scripts : scripts;
					HouseEdits = forward ? after_sections : sections;
					if (Scripts) Select_Script_Section(ScriptIndex);
				});
				VisualTeam = key;
				Status = "Saved team " + key + '.';
				return;
			}

			int const delay = Read_Number(Trim(VisualDelay));
			if (VisualEvent == "time" && (delay < 0 || delay > 3600)) {
				Status = "Enter a delay from 0 to 3600 seconds.";
				return;
			}
			bool const team_action = VisualAction == "team" || VisualAction == "reinforce";
			if ((VisualAction == "lightning" && !has_waypoint)
				|| (team_action && (!listed("TeamTypes", VisualTeam) || VisualTeam.size() < 3))) {
				Status = "This action needs an existing waypoint or a named team ID.";
				return;
			}
			int house_parameter = 0;
			if (VisualAction == "win" || VisualAction == "lose") {
				house_parameter = HouseTypeClass::From_Name(std::string(house).c_str());
				if (house_parameter == HOUSE_NONE) {
					Status = "Win and lose actions need a house from the active rules.";
					return;
				}
			}
			auto const event = VisualEvent == "time" ? TEVENT_TIME : TEVENT_ANY;
			auto const action = VisualAction == "team" ? TACTION_CREATE_TEAM : VisualAction == "reinforce" ? TACTION_REINFORCEMENTS
				: VisualAction == "win" ? TACTION_WIN : VisualAction == "lose" ? TACTION_LOSE : TACTION_ION_LIGHTNING_STRIKE;
			std::array<std::string, 4> const rows{
				std::string(house) + ",<none>," + std::string(name) + ",0,1,1,1,0",
				"1," + std::to_string(event) + ",0," + std::to_string(VisualEvent == "time" ? delay : 0),
				"1," + std::to_string(action) + (team_action ? ",1," + VisualTeam : ",0," + std::to_string(house_parameter)) + ",0,0,0,0,"
					+ (VisualAction == "lightning" ? Waypoint_To_Name(waypoint) : "0"),
				"0," + std::string(name) + ',' + key,
			};
			auto updated = ScriptEdits;
			for (int section = 0; section < static_cast<int>(rows.size()); ++section) {
				EditorRecords records;
				if (!Load_Records(section, records)) return;
				records.emplace_back(key, rows[section]);
				updated[section] = Write_Editor_Records(records);
			}
			auto before = std::exchange(ScriptEdits, std::move(updated));
			Remember_Scripts(std::move(before));
			Status = "Created trigger and tag " + key + '.';
		}

		void Add_Lightning()
		{
			int const waypoint = Read_Number(Trim(Slot));
			int const delay = Read_Number(Trim(Delay));
			auto const target = std::find_if(Rows.begin(), Rows.end(), [&](WaypointRow const & row) { return(row.Slot == waypoint); });
			if (target == Rows.end() || !Preview.Inside(target->X, target->Y) || delay < 0 || delay > 3600) {
				Status = "Set an existing waypoint ID and a delay from 0 to 3600 seconds.";
				return;
			}

			auto const draft = Build_Copy();
			if (!draft) return;
			BufferStraw input(draft->data(), static_cast<int>(draft->size()));
			INIClass ini;
			ini.Load(input);
			std::string key;
			for (unsigned id = 0x7E000000; id < 0x7E010000; ++id) {
				char candidate[9];
				std::snprintf(candidate, sizeof(candidate), "%08X", id);
				if (!ini.Is_Present("Triggers", candidate) && !ini.Is_Present("Events", candidate)
					&& !ini.Is_Present("Actions", candidate) && !ini.Is_Present("Tags", candidate)) {
					key = candidate;
					break;
				}
			}
			if (key.empty()) {
				Status = "No free trigger ID is available.";
				return;
			}
			std::array<std::string, 4> const records = {
				"Spawn1,<none>,Lightning " + key + ",0,1,1,1,0",
				"1," + std::to_string(TEVENT_TIME) + ",0," + std::to_string(delay),
				"1," + std::to_string(TACTION_ION_LIGHTNING_STRIKE) + ",0,0,0,0,0,0," + Waypoint_To_Name(waypoint),
				"0,Lightning " + key + ',' + key,
			};
			auto previous = ScriptEdits;
			for (int index = 0; index < static_cast<int>(records.size()); ++index) {
				Select_Script_Section(index);
				if (!ScriptAvailable) {
					ScriptEdits = previous;
					return;
				}
				ScriptEdits[index] = ScriptText + key + '=' + records[index] + '\n';
			}
			Remember_Scripts(std::move(previous));
			Status = "Scheduled a lightning strike for skirmish.";
		}

		void Paint(int x, int y)
		{
			if (ToolTab == "scenery") {
				if (SelectedAsset >= 0) Place_Object(x, y, Preview.Assets[SelectedAsset]);
				else Status = "Select a scenery object first.";
				return;
			}
			if (SelectedAsset >= 0 && !Stamp_Fits(x, y)) {
				Status = "The full tile footprint must fit on the map at this height.";
				return;
			}
			WasTerrainChanged = TerrainChanged;
			Paint_Brush(x, y);
			Finish_Stroke();
		}

		void Paint_Brush(int x, int y)
		{
			if (SelectedAsset >= 0 && ActiveStamp.Cells.size() > 1) {
				int const base = Preview.Elevation(x, y);
				for (auto const & cell : ActiveStamp.Cells) {
					Paint_Tile(x + cell.X, y + cell.Y, Preview.Assets[SelectedAsset].Tile, cell.Index, base + cell.Height);
				}
				return;
			}
			int const start = -(BrushSize - 1) / 2;
			for (int dy = start; dy < start + BrushSize; ++dy) {
				for (int dx = start; dx < start + BrushSize; ++dx) {
					int const px = x + dx, py = y + dy;
					if (!Preview.Inside(px, py)) continue;
					if (WaterBrush) {
						if (Preview.WaterTiles.Count == 0) Status = "This theater has no water tile set.";
						else {
							Paint_Tile(px, py, Preview.WaterTiles.Base, 0, -1, false);
							Status = "Water painted.";
						}
					} else if (SelectedAsset < 0) Paint_Tile(px, py, -1, 0, -1);
					else {
						auto const & cell = ActiveStamp.Cells.front();
						int const height = Preview.Elevation(px, py);
						if (height + cell.Height <= 20) Paint_Tile(px, py, Preview.Assets[SelectedAsset].Tile, cell.Index, height);
					}
				}
			}
		}

		void Match_Shores()
		{
			std::set<int> edges;
			for (auto const & entry : Stroke) {
				int const x = entry.first % 512, y = entry.first / 512;
				for (auto const & step : TunnelSteps) {
					int const nx = x + step[0], ny = y + step[1];
					if (Preview.Inside(nx, ny)) edges.insert(ny * 512 + nx);
				}
			}
			for (int edge : edges) {
				int const x = edge % 512, y = edge / 512;
				if (!Preview.WaterTiles.Contains(Preview.Tile_Id(x, y))) continue;
				int const elevation = Preview.Elevation(x, y);
				bool placed = false;
				for (auto const & [tile, footprint] : Preview.Shores) {
					for (auto const & anchor : footprint) {
						if (!anchor.Water) continue;
						int const sx = x - anchor.X, sy = y - anchor.Y;
						bool const fits = std::ranges::all_of(footprint, [&](auto const & cell) {
							int const px = sx + cell.X, py = sy + cell.Y;
							if (!Preview.Inside(px, py) || Preview.Elevation(px, py) != elevation) return(false);
							int const existing = Preview.Tile_Id(px, py);
							return(cell.Water ? Preview.WaterTiles.Contains(existing) : existing == -1 || Preview.ClearTiles.Contains(existing));
						});
						if (!fits) continue;
						for (auto const & cell : footprint) Paint_Tile(sx + cell.X, sy + cell.Y, tile, cell.Index, elevation, false);
						placed = true;
						break;
					}
					if (placed) break;
				}
			}
		}

		void Apply_Terrain_Transitions()
		{
			constexpr std::array<std::array<int, 2>, 4> neighbors{{{-1,0}, {0,-1}, {0,1}, {1,0}}};
			constexpr std::array<int, 16> variant{16,15,8,7,14,13,6,5,12,11,4,3,10,9,2,0};
			std::set<int> candidates;
			for (auto const & [cell, change] : Stroke) {
				int const x = cell % 512, y = cell / 512;
				candidates.insert(cell);
				for (auto const & offset : neighbors) {
					int const nx = x + offset[0], ny = y + offset[1];
					if (Preview.Inside(nx, ny)) candidates.insert(ny * 512 + nx);
				}
			}
			for (auto const & [material, border] : Preview.TransitionSets) {
				if (material.Count == 0 || border.Count < 16) continue;
				auto matches = [&](int tile) { return(material.Contains(tile) || border.Contains(tile)); };
				for (int cell : candidates) {
					int const x = cell % 512, y = cell / 512;
					int const current = Preview.Tile_Id(x, y);
					if (!matches(current)) continue;
					unsigned mask = 0;
					for (std::size_t i = 0; i < neighbors.size(); ++i) {
						int const nx = x + neighbors[i][0], ny = y + neighbors[i][1];
						if (Preview.Inside(nx, ny) && Preview.Elevation(nx, ny) == Preview.Elevation(x, y)
							&& matches(Preview.Tile_Id(nx, ny))) mask |= 1u << i;
					}
					int const piece = variant[mask];
					int const next = piece ? border.Base + piece - 1 : material.Base;
					if (current != next && (piece || border.Contains(current))) Paint_Tile(x, y, next, 0, -1, false);
				}
			}
		}

		void Paint_Tile(int x, int y, int tile, int subtile, int elevation, bool report = true)
		{
			int const index = y * 512 + x;
			auto const before = Preview.Tile_State(index);
			if (!Preview.Apply_Brush(x, y, 0, tile, subtile, elevation)) {
				Status = "This brush is unavailable for the map's terrain format or theater.";
				return;
			}
			if (report) {
				X = std::to_string(x);
				Y = std::to_string(y);
			}
			auto const after = Preview.Tile_State(index);
			if (before == after) return;
			auto const entry = Stroke.try_emplace(index, EditorTileChange{before, after});
			entry.first->second.After = after;
			PendingPaint[index] = after.Color - 256;
			FieldsChanged = PreviewChanged = TerrainChanged = true;
			if (report) Status = (tile < 0 ? std::string(Brushes[0].Name) : Preview.Assets[SelectedAsset].Name)
				+ " placed at " + X + ", " + Y + '.';
		}

		std::optional<std::string> Build_Copy()
		{
			auto data = Document.Export(Changes);
			for (auto const & [index, text] : ScriptEdits) {
				auto const & section = ScriptSections[index];
				std::string replacement = '[' + std::string(section.Name) + ']' + Document.Newline;
				for (char c : text) {
					if (c == '\n') replacement += Document.Newline;
					else replacement += c;
				}
				if (!Replace_Section(data, section.Key, replacement, Document.Newline)) {
					Status = "Could not export a duplicate scripting section.";
					return(std::nullopt);
				}
			}
			for (auto const & [name, text] : HouseEdits) {
				std::string key = name;
				for (char & c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				std::string replacement = '[' + name + ']' + Document.Newline;
				for (char c : text) {
					if (c == '\n') replacement += Document.Newline;
					else replacement += c;
				}
				if (!Replace_Section(data, key, replacement, Document.Newline)) {
					Status = "Could not export a duplicate house section.";
					return(std::nullopt);
				}
			}
			if (!OverlayEdits.empty()) {
				auto types = OverlayTypesBase;
				auto images = OverlayDataBase;
				for (auto const & [cell, overlay] : OverlayEdits) {
					types[cell] = overlay.Type;
					images[cell] = overlay.Data;
				}
				if (!Write_Overlay_Layer(data, Document.Newline, "OverlayPack", "overlaypack", types)
					|| !Write_Overlay_Layer(data, Document.Newline, "OverlayDataPack", "overlaydatapack", images)) {
					Status = "Could not export the overlay layers.";
					return(std::nullopt);
				}
			}
			if (TerrainChanged && !Preview.Write_Terrain(data, Document.Newline)) {
				Status = "Could not export the terrain section.";
				return(std::nullopt);
			}
			return(data);
		}

		void Save_Map(bool copy, bool overwrite)
		{
			FS::path output = WorkingPath;
			try {
				if (!overwrite) {
					std::string_view path = Trim(Destination);
					if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
					output = FS::absolute(FS::path(std::u8string(path.begin(), path.end())));
				}
				auto const extension = output.extension().string();
				if (!Equal_Section(extension, ".mpr") && !Equal_Section(extension, ".map")) {
					Status = "Enter a path ending in .map or .mpr.";
					return;
				}
			} catch (std::exception const &) {
				Status = "Enter a valid map path.";
				return;
			}
			auto const data = Build_Copy();
			if (!data) return;
			if (overwrite) {
				std::error_code error;
				auto const size = FS::file_size(output, error);
				std::ifstream stream(output, std::ios::binary);
				if (error || size != SavedContents.size() || !stream
					|| std::string(std::istreambuf_iterator<char>(stream), {}) != SavedContents) {
					Status = "The working file changed on disk. Use Save As.";
					return;
				}
				stream.close();
				auto temporary = output;
				temporary += ".opents-save";
				if (!Write_New_Map_File(temporary, *data, Status)) return;
				FS::rename(temporary, output, error);
				if (error) {
					FS::remove(temporary, error);
					Status = "Could not replace the working map.";
					return;
				}
			} else if (!Write_New_Map_File(output, *data, Status)) return;

			if (!copy) {
				WorkingPath = std::move(output);
				SavedContents = *data;
				SavedRevision = Revision;
				Draft = false;
				End_Recovery();
			}
			FilePanel.clear();
			Status = copy ? "Map copy exported." : "Map saved.";
		}

		void Update_Rows()
		{
			PreviewChanged = true;
			Rows.clear();
			for (int slot = 0; slot < WAYPT_COUNT; ++slot) {
				auto const changed = Changes.find(slot);
				int cell = Document.Waypoints[slot] ? Document.Waypoints[slot]->Cell : 0;
				if (changed != Changes.end()) cell = changed->second;
				if (cell) Rows.push_back({slot, cell % 1000, cell / 1000});
			}
		}

		ScenarioDocument Document;
		bool TerrainChanged = false;
		bool ScriptAvailable = true;
		std::map<int, std::string> ScriptEdits;
		std::map<int, int> Changes;
};


struct EditorAssetRow
{
	int Index;
	std::string Name;
	std::string File;
};

class MapEditorView final : public UIRmlViewClass
{
	public:
		explicit MapEditorView(MapEditorPresenter & presenter) :
			UIRmlViewClass(presenter, "mapedit.rml", "mapedit"), Data(presenter)
		{
		}

		void Sync() override
		{
			Sync_Assets();
			Model.DirtyVariable("status");
			Model.DirtyVariable("rows");
			Model.DirtyVariable("loaded");
			ConfirmDiscard = !Data.PendingAction.empty();
			HasChanges = Data.Has_Unexported_Changes();
			Model.DirtyVariable("confirmdiscard");
			Model.DirtyVariable("playtestissues");
			Model.DirtyVariable("haschanges");
			Model.DirtyVariable("recoveryavailable");
			Model.DirtyVariable("setup");
			Model.DirtyVariable("snow");
			Model.DirtyVariable("tilemode");
			Model.DirtyVariable("scripts");
			Model.DirtyVariable("scriptindex");
			Model.DirtyVariable("testsetup");
			Model.DirtyVariable("groupreferences");
			Model.DirtyVariable("playtestlogopen");
			Model.DirtyVariable("playtestlog");
			Model.DirtyVariable("visualscript");
			for (auto const name : {"triggerinspector", "triggerchoices", "triggerevents", "triggeractions", "triggerlinks",
				"inspectedtrigger", "selectedstep", "selectedstepaction", "steptype", "stepcode", "stepvalue", "stepwaypoint"})
				Model.DirtyVariable(name);
			Model.DirtyVariable("visualwaypoint");
			Model.DirtyVariable("visualteam");
			if (Data.ScriptTextChanged) {
				Model.DirtyVariable("scripttext");
				Data.ScriptTextChanged = false;
			}
			Model.DirtyVariable("grid");
			Model.DirtyVariable("tooltab");
			Model.DirtyVariable("inspectscenery");
			Model.DirtyVariable("movingobject");
			Model.DirtyVariable("ionlighting");
			Model.DirtyVariable("autotransition");
			Model.DirtyVariable("actorcategory");
			Model.DirtyVariable("actorspotstatus");
			Model.DirtyVariable("tagchoices");
			Model.DirtyVariable("tagerase");
			Model.DirtyVariable("showtags");
			Model.DirtyVariable("taginspect");
			if (!Data.TriggerToFocus.empty() && Data.Scripts) {
				auto * text = rmlui_dynamic_cast<Rml::ElementFormControlTextArea *>(Document()->GetElementById("script-records"));
				if (text && text->GetValue() == Data.ScriptText) {
					std::string const key = Data.TriggerToFocus + '=';
					std::size_t start = 0;
					while (start < Data.ScriptText.size()) {
						auto const end = Data.ScriptText.find('\n', start);
						if (std::string_view(Data.ScriptText).substr(start).starts_with(key)) {
							text->Focus();
							text->SetSelectionRange(static_cast<int>(start),
								static_cast<int>(end == std::string::npos ? Data.ScriptText.size() : end));
							break;
						}
						if (end == std::string::npos) break;
						start = end + 1;
					}
					Data.TriggerToFocus.clear();
				}
			}
			Model.DirtyVariable("balanceopen");
			Model.DirtyVariable("balancetext");
			if (Data.TagChoicesChanged) {
				Model.DirtyVariable("celltag");
				Data.TagChoicesChanged = false;
			}
			Model.DirtyVariable("actorinspect");
			Model.DirtyVariable("actorkey");
			Model.DirtyVariable("tunnelkey");
			Model.DirtyVariable("overlaybrush");
			Model.DirtyVariable("waterbrush");
			Model.DirtyVariable("autoshore");
			Model.DirtyVariable("regionpaste");
			ClipboardAvailable = Data.Has_Clipboard();
			Model.DirtyVariable("clipboardavailable");
			if (Data.HouseChanged) {
				for (auto const name : {"housechoices", "housename", "housefields"}) Model.DirtyVariable(name);
				Data.HouseChanged = false;
			}
			if (Data.BoundsChanged) {
				for (auto const name : {"boundswidth", "boundsheight", "boundsx", "boundsy", "boundslw", "boundslh"}) Model.DirtyVariable(name);
				Data.BoundsChanged = false;
			}
			if (Data.LightsChanged) {
				Model.DirtyVariable("lights");
				Data.LightsChanged = false;
			}
			if (Data.ActorFieldsChanged) {
				for (auto const name : {"actortypes", "actorhouses", "actortype", "actorowner", "actorhealth", "actorfacing", "actortag", "actorspot"}) Model.DirtyVariable(name);
				Data.ActorFieldsChanged = false;
			}
			Model.DirtyVariable("objectname");
			Model.DirtyVariable("filepanel");
			Model.DirtyVariable("maptitle");
			Model.DirtyVariable("mapdetails");
			char zoom[16];
			std::snprintf(zoom, sizeof(zoom), "%.1f", Data.Zoom);
			ZoomLabel = zoom;
			if (ZoomLabel.ends_with(".0")) ZoomLabel.resize(ZoomLabel.size() - 2);
			Model.DirtyVariable("zoomlevel");
			UndoAvailable = Data.Can_Undo();
			RedoAvailable = Data.Can_Redo();
			Model.DirtyVariable("canundo");
			Model.DirtyVariable("canredo");
			if (Data.PreviewChanged) {
				if (auto * surface = Find_Surface("canvas")) {
					surface->Set_Image(Data.Canvas_Width(), Data.Canvas_Height(), Data.Draw_View(false));
				}
				if (auto * minimap = Find_Surface("minimap")) {
					minimap->Set_Image(192, std::max(1, 192 * Data.Canvas_Height() / Data.Canvas_Width()), Data.Draw_View(true));
				}
				Data.PreviewChanged = false;
			}
			Model.DirtyVariable("selected");
			if (Data.FieldsChanged) {
				Model.DirtyVariable("slot");
				Model.DirtyVariable("destination");
				Model.DirtyVariable("x");
				Model.DirtyVariable("y");
				Data.FieldsChanged = false;
			}
		}

		void Sync_Assets()
		{
			if (Data.ToolTab != "tiles" && Data.ToolTab != "scenery") {
				SeenTab.clear();
				GhostAsset = -1;
				if (auto * ghost = Find_Surface("asset-ghost")) ghost->SetClass("shown", false);
				return;
			}
			bool const refresh = SeenCatalog != Data.CatalogRevision || SeenTab != Data.ToolTab;
			if (refresh) {
				SeenCatalog = Data.CatalogRevision;
				SeenTab = Data.ToolTab;
				AssetCategories.clear();
				AssetCategories.emplace_back();
				for (auto const & asset : Data.Preview.Assets) {
					if ((asset.Tile < 0) == (SeenTab == "scenery")) AssetCategories.push_back(asset.Category);
				}
				std::sort(AssetCategories.begin(), AssetCategories.end());
				AssetCategories.erase(std::unique(AssetCategories.begin(), AssetCategories.end()), AssetCategories.end());
				Model.DirtyVariable("assetcategories");
				Model.DirtyVariable("assetcategory");
				Model.DirtyVariable("assetsearch");
			}
			if (refresh || SeenSearch != Data.AssetSearch || SeenCategory != Data.AssetCategory || SeenPage != Data.AssetPage) {
				if (SeenSearch != Data.AssetSearch || SeenCategory != Data.AssetCategory) Data.AssetPage = 0;
				SeenSearch = Data.AssetSearch;
				SeenCategory = Data.AssetCategory;
				std::string query = SeenSearch;
				auto lower = [](std::string & text) {
					for (auto & c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				};
				lower(query);
				std::vector<int> matches;
				for (int i = 0; i < static_cast<int>(Data.Preview.Assets.size()); ++i) {
					auto const & asset = Data.Preview.Assets[i];
					if ((asset.Tile < 0) != (SeenTab == "scenery")) continue;
					if (!SeenCategory.empty() && asset.Category != SeenCategory) continue;
					std::string text = asset.Name + ' ' + asset.File;
					lower(text);
					if (text.find(query) != std::string::npos) matches.push_back(i);
				}
				constexpr int page_size = 6;
				Data.LastAssetPage = matches.empty() ? 0 : (static_cast<int>(matches.size()) - 1) / page_size;
				SeenPage = Data.AssetPage = std::min(Data.AssetPage, Data.LastAssetPage);
				AssetRows.clear();
				AssetThumbnails.clear();
				for (int i = SeenPage * page_size; i < static_cast<int>(matches.size()) && i < (SeenPage + 1) * page_size; ++i) {
					int const index = matches[i];
					auto const & asset = Data.Preview.Assets[index];
					AssetRows.push_back({index, asset.Name, asset.File});
					auto loaded = Data.Preview.Load_Asset(index);
					AssetThumbnails.push_back(asset.Tile < 0 ? std::move(loaded.second) : std::move(loaded.first.Image));
				}
				Model.DirtyVariable("assetrows");
				Model.DirtyVariable("assetpage");
				Model.DirtyVariable("assetlast");
				ThumbnailsDirty = true;
			} else if (ThumbnailsDirty) {
				ThumbnailsDirty = false;
				for (std::size_t i = 0; i < AssetRows.size(); ++i) {
					auto * surface = Find_Surface(("asset-" + std::to_string(AssetRows[i].Index)).c_str());
					if (!surface) { ThumbnailsDirty = true; continue; }
					auto const & image = AssetThumbnails[i];
					surface->Set_Image(image.Width, image.Height, image.Pixels);
				}
			}
			if (GhostAsset != Data.SelectedAsset || refresh) {
				GhostAsset = Data.SelectedAsset;
				if (auto * ghost = Find_Surface("asset-ghost")) {
					auto const & image = Data.Ghost();
					ghost->Set_Image(image.Width, image.Height, GhostAsset < 0 ? std::vector<std::uint8_t>{} : image.Pixels);
					ghost->SetClass("shown", false);
				}
				Model.DirtyVariable("selectedasset");
			}
			Model.DirtyVariable("brushsize");
			SingleTileBrush = Data.Can_Stroke();
			Model.DirtyVariable("singlebrush");
		}

		bool Handle_Pointer_Gesture(WindowEvent const & event, int x, int y) override
		{
			if (!Data.Loaded || Data.Setup || Data.Scripts || !Data.FilePanel.empty() || !Data.PendingAction.empty() || !Data.PlaytestIssues.empty() || Data.BalanceOpen || Data.TestSetup || Data.PlaytestLogOpen) return(false);

			auto * canvas = Find_Surface("canvas");
			Rml::Vector2f fraction, fitted;
			if (!Fit_Point(canvas, Rml::Vector2f(x, y), fraction, fitted)) return(false);
			if (Stroking || Panning) return(true);

			if (event.Type == WINDOW_EVENT_PINCH) {
				if (!std::isfinite(event.PinchScale) || event.PinchScale <= 0.0f) return(true);
				Data.Set_Zoom(Data.Zoom * event.PinchScale, fraction.x, fraction.y);
			} else {
				float const pixels = event.PreciseScrollPixels;
				if (!std::isfinite(pixels)) return(true);
				Data.Pan(event.Horizontal ? pixels / fitted.x : 0.0f, event.Horizontal ? 0.0f : -pixels / fitted.y);
			}
			Update_Cursor(canvas, fraction, fitted, true);
			return(true);
		}

	protected:
		void Loaded() override
		{
			Document()->AddEventListener(Rml::EventId::Keydown, this);
			Document()->AddEventListener(Rml::EventId::Mouseup, this);
			Document()->AddEventListener(Rml::EventId::Mousescroll, this);
		}

		void ProcessEvent(Rml::Event & event) override
		{
			auto const kind = event.GetId();
			if (kind == Rml::EventId::Keydown
				&& event.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_S
				&& (event.GetParameter<int>("ctrl_key", 0) || event.GetParameter<int>("meta_key", 0))) {
				Queue(event.GetParameter<int>("shift_key", 0) ? "save-as" : "save", 0);
				event.StopPropagation();
				return;
			}
			int const button = event.GetParameter<int>("button", -1);
			if (kind == Rml::EventId::Mouseup && (button == 1 || button == 2)) Panning = false;
			if (!Data.Loaded || Data.Setup || Data.Scripts || !Data.FilePanel.empty() || !Data.PendingAction.empty() || !Data.PlaytestIssues.empty() || Data.BalanceOpen || Data.TestSetup || Data.PlaytestLogOpen) {
				UIRmlViewClass::ProcessEvent(event);
				return;
			}

			Rml::Vector2f const position(event.GetParameter<float>("mouse_x", 0.0f), event.GetParameter<float>("mouse_y", 0.0f));
			auto * canvas = Find_Surface("canvas");
			Rml::Vector2f fraction, fitted;
			bool const on_map = Fit_Point(canvas, position, fraction, fitted);
			if (kind == Rml::EventId::Mousemove) Update_Cursor(canvas, fraction, fitted, on_map && !Panning);

			if (kind == Rml::EventId::Mousedown && on_map && (button == 1 || button == 2)) {
				Panning = true;
				PanFrom = position;
				event.StopPropagation();
				return;
			}
			bool const ending_stroke = kind == Rml::EventId::Mouseup && button == 0;
			if (ActorDragFrom && (kind == Rml::EventId::Mousemove || ending_stroke)) {
				auto const cell = on_map ? Data.Hit(fraction.x, fraction.y) : Point2D(-1, -1);
				Point2D const anchor(on_map ? cell.X + Data.ActorCell.X - ActorDragFrom->X : -1,
					on_map ? cell.Y + Data.ActorCell.Y - ActorDragFrom->Y : -1);
				if (kind == Rml::EventId::Mousemove) {
					if (Data.DragPreview != anchor) {
						Data.Update_Drag_Preview(anchor, ActorDragCopy);
						Data.PreviewChanged = true;
					}
				} else {
					auto const from = *std::exchange(ActorDragFrom, std::nullopt);
					Data.DragPreview = Point2D(-1, -1);
					Data.PreviewChanged = true;
					if (Data.Preview.Inside(anchor.X, anchor.Y) && cell != from) {
						Queue(ActorDragCopy ? "actor-duplicate" : "actor-drag", anchor.Y * 1000 + anchor.X);
					}
				}
				return;
			}
			if (ending_stroke && RegionDragFrom) {
				auto const from = *std::exchange(RegionDragFrom, std::nullopt);
				auto const cell = on_map ? Data.Hit(fraction.x, fraction.y) : from;
				if (Data.Preview.Inside(cell.X, cell.Y)) {
					if (cell != from) Queue("region-origin", from.Y * 1000 + from.X);
					Queue("cell", cell.Y * 1000 + cell.X);
				}
				return;
			}
			if (Stroking && (kind == Rml::EventId::Mousemove || ending_stroke)) {
				if (on_map) {
					auto const cell = Data.Hit(fraction.x, fraction.y);
					if (Data.Preview.Inside(cell.X, cell.Y)) Queue("stroke-move", cell.Y * 1000 + cell.X);
				}
				if (ending_stroke) {
					Queue("stroke-end");
					Stroking = false;
				}
				return;
			}
			if (kind == Rml::EventId::Mousemove && Panning && fitted.x > 0 && fitted.y > 0) {
				Data.Pan((PanFrom.x - position.x) / fitted.x, (PanFrom.y - position.y) / fitted.y);
				PanFrom = position;
				return;
			}
			if (kind == Rml::EventId::Mousescroll && on_map) {
				float const wheel = event.GetParameter<float>("wheel_delta_y", 0.0f);
				if (wheel != 0) Data.Set_Zoom(Data.Zoom + (wheel < 0 ? 1 : -1), fraction.x, fraction.y);
				event.StopPropagation();
				return;
			}
			if (kind == Rml::EventId::Mousedown && button == 0) {
				if (on_map) {
					auto const cell = Data.Hit(fraction.x, fraction.y);
					if (Data.Preview.Inside(cell.X, cell.Y)) {
						if (Data.PickScriptPoint) {
							Queue("cell", cell.Y * 1000 + cell.X);
							return;
						}
						if (Data.ToolTab == "actors" && Data.ActorInspect) {
							ActorDragFrom = cell;
							ActorDragCopy = event.GetParameter<int>("shift_key", 0) != 0;
							Queue("cell", cell.Y * 1000 + cell.X);
							return;
						}
						if (Data.ToolTab == "regions" && !Data.RegionPaste) {
							RegionDragFrom = cell;
							return;
						}
						Stroking = Data.Can_Stroke();
						Queue(Stroking ? "stroke-start" : "cell", cell.Y * 1000 + cell.X);
					}
					return;
				}
				if (Fit_Point(Find_Surface("minimap"), position, fraction, fitted)) {
					Data.Center(fraction.x, fraction.y);
					return;
				}
			}
			UIRmlViewClass::ProcessEvent(event);
		}

		void Update_Cursor(Rml::Element * canvas, Rml::Vector2f fraction, Rml::Vector2f fitted, bool active)
		{
			auto * cursor = Document()->GetElementById("brush-cursor");
			if (!cursor) return;
			Point2D cell(-1, -1);
			if (active) cell = Data.Hit(fraction.x, fraction.y);
			bool const visible = Data.Preview.Inside(cell.X, cell.Y);
			cursor->SetClass("shown", visible);
			auto * ghost = Find_Surface("asset-ghost");
			if (ghost) ghost->SetClass("shown", false);
			std::string const status = visible ? std::to_string(cell.X) + ", " + std::to_string(cell.Y) : "";
			if (status != CursorCell) {
				CursorCell = status;
				Model.DirtyVariable("cursorcell");
			}
			if (!visible) return;
			auto const point = Data.Project_Cell(cell);
			auto const box = canvas->GetBox().GetSize(Rml::BoxArea::Content);
			float const x = (box.x - fitted.x) * 0.5f + point.x * fitted.x;
			float const y = (box.y - fitted.y) * 0.5f + point.y * fitted.y;
			cursor->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
			cursor->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
			if (!ghost || Data.SelectedAsset < 0 || (Data.ToolTab == "scenery" && Data.InspectScenery)) return;
			auto const & image = Data.Ghost();
			if (image.Pixels.empty()) return;
			float const scale = Data.Ghost_Scale(fitted.x);
			ghost->SetProperty(Rml::PropertyId::Left, Rml::Property(x - image.AnchorX * scale, Rml::Unit::PX));
			ghost->SetProperty(Rml::PropertyId::Top, Rml::Property(y - image.AnchorY * scale, Rml::Unit::PX));
			ghost->SetProperty(Rml::PropertyId::Width, Rml::Property(image.Width * scale, Rml::Unit::PX));
			ghost->SetProperty(Rml::PropertyId::Height, Rml::Property(image.Height * scale, Rml::Unit::PX));
			ghost->SetClass("shown", true);
			ghost->SetClass("blocked", Data.ToolTab == "tiles" && !Data.Stamp_Fits(cell.X, cell.Y));
		}

		UIRmlSurfaceElementClass * Find_Surface(char const * name) const
		{
			return(rmlui_dynamic_cast<UIRmlSurfaceElementClass *>(Document()->GetElementById(name)));
		}

		bool Fit_Point(UIRmlSurfaceElementClass * surface, Rml::Vector2f position, Rml::Vector2f & fraction, Rml::Vector2f & fitted) const
		{
			if (!surface) return(false);
			auto const area = surface->GetBox().GetSize(Rml::BoxArea::Content);
			int x, y, width, height;
			if (!UI_Surface_Fit(Data.Canvas_Width(), Data.Canvas_Height(), static_cast<int>(area.x), static_cast<int>(area.y),
				x, y, width, height)) return(false);
			fitted = Rml::Vector2f(width, height);
			auto const offset = surface->GetAbsoluteOffset();
			fraction = Rml::Vector2f((position.x - offset.x - x) / width, (position.y - offset.y - y) / height);
			return(fraction.x >= 0 && fraction.x < 1 && fraction.y >= 0 && fraction.y < 1);
		}

		bool Bind(Rml::DataModelConstructor & model) override
		{
			auto tag = model.RegisterStruct<EditorTagChoice>();
			if (!tag) return(false);
			tag.RegisterMember("id", &EditorTagChoice::Id);
			tag.RegisterMember("label", &EditorTagChoice::Label);
			auto light = model.RegisterStruct<LightingField>();
			if (!light) return(false);
			light.RegisterMember("label", &LightingField::Label);
			light.RegisterMember("value", &LightingField::Value);
			auto issue = model.RegisterStruct<PlaytestIssue>();
			if (!issue) return(false);
			issue.RegisterMember("slot", &PlaytestIssue::Slot);
			issue.RegisterMember("text", &PlaytestIssue::Text);
			issue.RegisterMember("blocking", &PlaytestIssue::Blocking);
			auto row = model.RegisterStruct<WaypointRow>();
			if (!row) return(false);
			row.RegisterMember("slot", &WaypointRow::Slot);
			row.RegisterMember("x", &WaypointRow::X);
			row.RegisterMember("y", &WaypointRow::Y);
			auto asset = model.RegisterStruct<EditorAssetRow>();
			if (!asset) return(false);
			asset.RegisterMember("index", &EditorAssetRow::Index);
			asset.RegisterMember("name", &EditorAssetRow::Name);
			asset.RegisterMember("file", &EditorAssetRow::File);
			return(model.RegisterArray<std::vector<EditorTagChoice>>() && model.RegisterArray<std::vector<LightingField>>() && model.RegisterArray<std::vector<std::string>>() && model.Bind("lights", &Data.Lights)
				&& model.Bind("housefields", &Data.HouseFields) && model.Bind("housechoices", &Data.HouseChoices) && model.Bind("housename", &Data.HouseName)
				&& model.Bind("boundswidth", &Data.BoundsWidth) && model.Bind("boundsheight", &Data.BoundsHeight)
				&& model.Bind("boundsx", &Data.BoundsX) && model.Bind("boundsy", &Data.BoundsY)
				&& model.Bind("boundslw", &Data.BoundsLocalWidth) && model.Bind("boundslh", &Data.BoundsLocalHeight)
				&& model.Bind("overlaybrush", &Data.OverlayBrush) && model.Bind("overlaydensity", &Data.OverlayDensity)
				&& model.Bind("waterbrush", &Data.WaterBrush) && model.Bind("autoshore", &Data.AutoShore)
				&& model.Bind("copyterrain", &Data.CopyTerrain) && model.Bind("copyoverlays", &Data.CopyOverlays)
				&& model.Bind("grouppath", &Data.GroupPath) && model.Bind("groupreferences", &Data.GroupReferences)
				&& model.Bind("copyobjects", &Data.CopyObjects) && model.Bind("pasteelevation", &Data.PasteElevation) && model.Bind("regionpaste", &Data.RegionPaste)
				&& model.Bind("clipboardavailable", &ClipboardAvailable) && model.Bind("ionlighting", &Data.IonLighting) && model.Bind("autotransition", &Data.AutoTransition)
				&& model.Bind("actorcategory", &Data.ActorCategory) && model.Bind("actorspot", &Data.ActorSpot)
				&& model.Bind("actorspotstatus", &Data.ActorSpotStatus) && model.Bind("celltag", &Data.CellTag)
				&& model.Bind("tagchoices", &Data.TagChoices) && model.Bind("tagerase", &Data.TagErase)
				&& model.Bind("showtags", &Data.ShowTags) && model.Bind("taginspect", &Data.TagInspect) && model.Bind("balanceopen", &Data.BalanceOpen)
				&& model.Bind("balancetext", &Data.BalanceText) && model.Bind("actorinspect", &Data.ActorInspect)
				&& model.Bind("actorkey", &Data.ActorKey) && model.Bind("actortype", &Data.ActorType)
				&& model.Bind("actortypes", &Data.ActorTypes) && model.Bind("actorhouses", &Data.ActorHouses)
				&& model.Bind("actorowner", &Data.ActorOwner) && model.Bind("actorhealth", &Data.ActorHealth)
				&& model.Bind("actorfacing", &Data.ActorFacing) && model.Bind("actortag", &Data.ActorTag)
				&& model.Bind("tunnelkey", &Data.TunnelKey) && model.RegisterArray<std::vector<PlaytestIssue>>() && model.Bind("playtestissues", &Data.PlaytestIssues)
				&& model.RegisterArray<std::vector<EditorAssetRow>>()
				&& model.Bind("assetrows", &AssetRows) && model.Bind("assetcategories", &AssetCategories)
				&& model.Bind("assetsearch", &Data.AssetSearch) && model.Bind("assetcategory", &Data.AssetCategory)
				&& model.Bind("assetpage", &Data.AssetPage) && model.Bind("assetlast", &Data.LastAssetPage)
				&& model.Bind("selectedasset", &Data.SelectedAsset) && model.Bind("brushsize", &Data.BrushSize)
				&& model.Bind("singlebrush", &SingleTileBrush) && model.RegisterArray<std::vector<WaypointRow>>()
				&& model.Bind("rows", &Data.Rows) && model.Bind("path", &Data.Path)
				&& model.Bind("slot", &Data.Slot) && model.Bind("x", &Data.X) && model.Bind("y", &Data.Y)
				&& model.Bind("status", &Data.Status) && model.Bind("loaded", &Data.Loaded)
				&& model.Bind("selected", &Data.SelectedSlot) && model.Bind("confirmdiscard", &ConfirmDiscard)
				&& model.Bind("haschanges", &HasChanges) && model.Bind("recoveryavailable", &Data.RecoveryAvailable)
				&& model.Bind("mapwidth", &Data.Width) && model.Bind("mapheight", &Data.Height)
				&& model.Bind("mapname", &Data.Name) && model.Bind("snow", &Data.Snow)
				&& model.Bind("setup", &Data.Setup) && model.Bind("destination", &Data.Destination)
				&& model.Bind("tilemode", &Data.TileMode)
				&& model.Bind("triggerinspector", &Data.TriggerInspector) && model.Bind("triggersearch", &Data.TriggerSearch)
				&& model.Bind("triggerchoices", &Data.TriggerChoices) && model.Bind("triggerevents", &Data.TriggerEvents)
				&& model.Bind("triggeractions", &Data.TriggerActions) && model.Bind("triggerlinks", &Data.TriggerLinks)
				&& model.Bind("inspectedtrigger", &Data.InspectedTrigger) && model.Bind("selectedstep", &Data.SelectedStep)
				&& model.Bind("selectedstepaction", &Data.SelectedStepAction) && model.Bind("steptype", &Data.StepType)
				&& model.Bind("stepcode", &Data.StepCode) && model.Bind("stepvalue", &Data.StepValue) && model.Bind("stepwaypoint", &Data.StepWaypoint)
				&& model.Bind("visualscript", &Data.VisualScript) && model.Bind("visualkind", &Data.VisualKind)
				&& model.Bind("visualname", &Data.VisualName) && model.Bind("visualhouse", &Data.VisualHouse)
				&& model.Bind("visualevent", &Data.VisualEvent) && model.Bind("visualdelay", &Data.VisualDelay)
				&& model.Bind("visualaction", &Data.VisualAction) && model.Bind("visualwaypoint", &Data.VisualWaypoint)
				&& model.Bind("visualteam", &Data.VisualTeam) && model.Bind("visualforce", &Data.VisualForce)
				&& model.Bind("visualteamscript", &Data.VisualTeamScript)
				&& model.Bind("playtestlogopen", &Data.PlaytestLogOpen) && model.Bind("playtestlog", &Data.PlaytestLog)
				&& model.Bind("testsetup", &Data.TestSetup) && model.Bind("scriptedtest", &Data.ScriptedTest)
				&& model.Bind("testslot", &Data.TestSlot) && model.Bind("testopponents", &Data.TestOpponents)
				&& model.Bind("scripts", &Data.Scripts) && model.Bind("scriptindex", &Data.ScriptIndex)
				&& model.Bind("scripttext", &Data.ScriptText) && model.Bind("effectdelay", &Data.Delay)
				&& model.Bind("grid", &Data.Grid) && model.Bind("canundo", &UndoAvailable)
				&& model.Bind("canredo", &RedoAvailable)
				&& model.Bind("inspectscenery", &Data.InspectScenery) && model.Bind("movingobject", &Data.MovingObject)
				&& model.Bind("objectname", &Data.SelectedObjectName) && model.Bind("tooltab", &Data.ToolTab) && model.Bind("filepanel", &Data.FilePanel)
				&& model.Bind("maptitle", &Data.MapTitle) && model.Bind("mapdetails", &Data.MapDetails)
				&& model.Bind("zoomlevel", &ZoomLabel) && model.Bind("cursorcell", &CursorCell));
		}

	private:
		MapEditorPresenter & Data;
		bool Panning = false;
		bool Stroking = false;
		std::optional<Point2D> RegionDragFrom, ActorDragFrom;
		bool ActorDragCopy = false;
		std::string CursorCell;
		std::string ZoomLabel = "1";
		bool ConfirmDiscard = false;
		bool HasChanges = false;
		bool UndoAvailable = false;
		bool RedoAvailable = false;
		Rml::Vector2f PanFrom;
		std::vector<EditorAssetRow> AssetRows;
		std::vector<std::string> AssetCategories;
		std::vector<EditorAssetImage> AssetThumbnails;
		std::string SeenSearch, SeenCategory, SeenTab;
		unsigned SeenCatalog = 0;
		int SeenPage = -1;
		int GhostAsset = -1;
		bool ThumbnailsDirty = false;
		bool SingleTileBrush = false;
		bool ClipboardAvailable = false;
};


}


namespace
{
std::unique_ptr<MapEditorPresenter> RetainedEditor;
}


bool UI_Map_Editor_Resume_Requested()
{
	return(RetainedEditor != nullptr);
}


void UI_Map_Editor_Log_Trigger(char const * id, char const * description, int frame, int action)
{
	if (!RetainedEditor || RetainedEditor->PlaytestLog.size() >= 500) return;
	RetainedEditor->PlaytestLog.push_back({id, "Frame " + std::to_string(frame) + " · "
		+ description + (action >= 0 ? " · " + Script_Step_Name(true, action) : " · Trigger fired")});
}

MapEditorPlaytest UI_Map_Editor_Dialog()
{
	if (!RetainedEditor) RetainedEditor = std::make_unique<MapEditorPresenter>();
	auto & presenter = *RetainedEditor;
	presenter.Result.reset();
	presenter.TestData.clear();
	presenter.PreviewChanged = presenter.Loaded;
	presenter.FieldsChanged = presenter.Loaded;
	Video_Set_Menu_Presentation(false);
	{
		MapEditorView view(presenter);
		UIShell.Run_Modal(view, [] {
			return(UI_Service_Game());
		});
	}
	Video_Set_Menu_Presentation(true);
	std::string test = std::move(presenter.TestData);
	MapEditorPlaytest result{std::move(test), presenter.ScriptedTest, Read_Number(presenter.TestSlot), Read_Number(presenter.TestOpponents)};
	if (result.Data.empty()) RetainedEditor.reset();
	else presenter.Status = "Returned from playtest.";
	return(result);
}
