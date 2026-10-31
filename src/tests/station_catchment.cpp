/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_catchment.cpp Tests for road-access coverage and persistent coverage pins. */

#include "../stdafx.h"
#include "../3rdparty/catch2/catch.hpp"
#include "../clear_map.h"
#include "../core/backup_type.hpp"
#include "../openttd.h"
#include "../landscape.h"
#include "../industry.h"
#include "../newgrf_airport.h"
#include "../rail.h"
#include "../road.h"
#include "../road_map.h"
#include "../settings_internal.h"
#include "../station_base.h"
#include "../station_catchment.h"
#include "../station_gui.h"
#include "../tilehighlight_func.h"
#include "../window_gui.h"
#include "../tunnelbridge_map.h"
#include "../viewport_func.h"

#include "../safeguards.h"

/** A small empty map with real road/station tiles, without a running game. */
class RoadCatchmentFixture {
	AutoRestoreBackup<StationSettings> settings{_settings_game.station};

public:
	RoadCatchmentFixture()
	{
		_settings_game.station.road_access_catchment = false;
		Map::Allocate(64, 64);
		for (TileIndex tile{0}; tile < Map::Size(); ++tile) MakeClear(tile, ClearGround::Grass, 3);
		ResetRoadTypes();
		ResetRailTypes();
		AirportSpec::ResetAirports();
	}

	~RoadCatchmentFixture()
	{
		for (const Station *st : Station::Iterate()) SetViewportCatchmentStation(st, false);
		_station_pool.CleanPool();
		_industry_pool.CleanPool();
		ClearPinnedStationCoverage();
		ResetStationRoadAccess();
	}

	static void Road(uint x, uint y, RoadBits bits = ROAD_X)
	{
		MakeRoadNormal(TileXY(x, y), bits, ROADTYPE_ROAD, INVALID_ROADTYPE, TownID::Invalid(), OWNER_NONE, OWNER_NONE);
	}

	static RoadCatchmentSource Source(uint x = 10, uint y = 10, uint distance = 6)
	{
		return {TileArea(TileXY(x, y), 1, 1), 2, distance, std::nullopt};
	}

	static BitmapTileArea Coverage(const RoadCatchmentSource &source)
	{
		return BuildRoadCatchment(std::span(&source, 1));
	}

	static Station *RailStation(uint x = 10, uint y = 10)
	{
		REQUIRE(Station::CanAllocateItem());
		Station *st = Station::Create(TileXY(x, y));
		st->facilities.Set(StationFacility::Train);
		st->train_station = st->spread = TileArea(st->xy, 1, 1);
		MakeRailStation(st->xy, OWNER_NONE, st->index, Axis::X, 0, RAILTYPE_RAIL);
		return st;
	}
};

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - facility budgets", "[station-catchment]")
{
	TileArea area(TileXY(10, 10), 1, 1);
	for (auto [railtype, distance] : {std::pair{RAILTYPE_RAIL, 6U}, {RAILTYPE_ELECTRIC, 6U}, {RAILTYPE_MONO, 7U}, {RAILTYPE_MAGLEV, 9U}}) {
		auto source = MakeRoadCatchmentSource(area, StationType::Rail, CA_UNMODIFIED, railtype);
		CHECK(source.local_radius == 2);
		CHECK(source.road_distance == distance);
	}
	CHECK(MakeRoadCatchmentSource(area, StationType::Airport, 4).road_distance == 6);
	CHECK(MakeRoadCatchmentSource(area, StationType::Airport, 5).road_distance == 7);
	CHECK(MakeRoadCatchmentSource(area, StationType::Airport, 10).road_distance == 12);
	CHECK(MakeRoadCatchmentSource(area, StationType::Dock).road_distance == 7);
	for (StationType type : {StationType::Bus, StationType::Truck}) {
		auto source = MakeRoadCatchmentSource(area, type);
		CHECK(source.local_radius == 4);
		CHECK(source.road_distance == 0);
		for (uint x = 11; x <= 25; ++x) Road(x, 10);
		auto covered = Coverage(source);
		CHECK(covered.Contains(TileXY(14, 14)));
		CHECK_FALSE(covered.Contains(TileXY(15, 10)));
	}
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - local area and connected road limit", "[station-catchment]")
{
	auto source = Source();
	auto covered = Coverage(source);
	CHECK(covered.Contains(TileXY(8, 8)));
	CHECK(covered.Contains(TileXY(12, 12)));
	CHECK_FALSE(covered.Contains(TileXY(13, 10)));

	for (uint x = 11; x <= 25; ++x) Road(x, 10);
	covered = Coverage(source);
	CHECK(covered.Contains(TileXY(16, 11))); // Six road tiles from the facility, then a building beside the road.
	CHECK(covered.Contains(TileXY(17, 10)));
	CHECK_FALSE(covered.Contains(TileXY(17, 11)));
	CHECK_FALSE(covered.Contains(TileXY(16, 12)));
	for (uint distance : {7U, 9U}) {
		source.road_distance = distance;
		covered = Coverage(source);
		CHECK(covered.Contains(TileXY(10 + distance, 11)));
		CHECK_FALSE(covered.Contains(TileXY(11 + distance, 11)));
	}
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - reciprocal connections and missing roads", "[station-catchment]")
{
	for (uint x = 11; x <= 20; ++x) Road(x, 10);
	Road(13, 10, ROAD_Y); // Geometrically adjacent but no connecting road bits.
	CHECK_FALSE(Coverage(Source()).Contains(TileXY(16, 11)));
	Road(13, 10);
	CHECK(Coverage(Source()).Contains(TileXY(16, 11)));
	MakeClear(TileXY(13, 10), ClearGround::Grass, 3);
	CHECK_FALSE(Coverage(Source()).Contains(TileXY(16, 11)));
	Road(13, 10);
	// A disconnected parallel street does not create another access point.
	for (uint x = 11; x <= 20; ++x) Road(x, 12);
	CHECK_FALSE(Coverage(Source()).Contains(TileXY(16, 13)));
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - junctions cycles and multiple facilities", "[station-catchment]")
{
	for (uint x = 11; x <= 20; ++x) Road(x, 10);
	for (uint x = 11; x <= 15; ++x) Road(x, 12);
	Road(11, 10, ROAD_X | DiagDirToRoadBits(DiagDirection::SE));
	Road(11, 11, ROAD_Y);
	Road(11, 12, ROAD_W);
	Road(15, 10, ROAD_X | DiagDirToRoadBits(DiagDirection::SE));
	Road(15, 11, ROAD_Y);
	Road(15, 12, ROAD_N);
	RoadCatchmentSource sources[] = {Source(10, 10, 4), Source(10, 10, 9)};
	auto covered = BuildRoadCatchment(sources);
	CHECK(covered.Contains(TileXY(19, 11)));
	CHECK_FALSE(covered.Contains(TileXY(20, 11)));
	std::reverse(std::begin(sources), std::end(sources));
	CHECK(BuildRoadCatchment(sources).Contains(TileXY(19, 11)));
	// The empty gap between distant parts of a joined station is not a facility.
	sources[0] = Source(30, 30);
	sources[1] = Source(40, 40);
	CHECK_FALSE(BuildRoadCatchment(sources).Contains(TileXY(35, 35)));
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - dock inland entrance", "[station-catchment]")
{
	auto source = Source();
	source.road_distance = 7;
	source.dock_direction = DiagDirection::NE; // Sea is to the north-east, road entrance to south-west (+x).
	for (uint x = 11; x <= 20; ++x) Road(x, 10);
	for (uint x = 3; x <= 9; ++x) Road(x, 10);
	auto covered = Coverage(source);
	CHECK(covered.Contains(TileXY(17, 11)));
	CHECK_FALSE(covered.Contains(TileXY(6, 11)));
	Road(11, 10, DiagDirToRoadBits(DiagDirection::SW)); // Missing bit toward the dock entrance.
	CHECK_FALSE(Coverage(source).Contains(TileXY(17, 11)));
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - bridges and tunnels consume their span", "[station-catchment]")
{
	bool tunnel = GENERATE(false, true);
	if (tunnel) {
		MakeRoadTunnel(TileXY(11, 10), OWNER_NONE, DiagDirection::SW, ROADTYPE_ROAD, INVALID_ROADTYPE);
		MakeRoadTunnel(TileXY(15, 10), OWNER_NONE, DiagDirection::NE, ROADTYPE_ROAD, INVALID_ROADTYPE);
	} else {
		MakeRoadBridgeRamp(TileXY(11, 10), OWNER_NONE, OWNER_NONE, OWNER_NONE, 0, DiagDirection::SW, ROADTYPE_ROAD, INVALID_ROADTYPE);
		MakeRoadBridgeRamp(TileXY(15, 10), OWNER_NONE, OWNER_NONE, OWNER_NONE, 0, DiagDirection::NE, ROADTYPE_ROAD, INVALID_ROADTYPE);
	}
	for (uint x = 16; x <= 20; ++x) Road(x, 10);
	auto covered = Coverage(Source());
	CHECK(covered.Contains(TileXY(16, 11)));
	CHECK_FALSE(covered.Contains(TileXY(17, 11)));
	CHECK_FALSE(covered.Contains(TileXY(13, 11))); // No access to buildings beneath the span.
	CHECK_FALSE(Coverage(Source(10, 10, 4)).Contains(TileXY(15, 11)));
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - map edges and prospective footprint", "[station-catchment]")
{
	Road(1, 0);
	Road(2, 0);
	auto covered = Coverage(Source(0, 0));
	CHECK(covered.Contains(TileXY(2, 1)));
	CHECK_FALSE(covered.Contains(TileXY(63, 0)));
	CHECK_FALSE(covered.Contains(TileXY(0, 63)));
	CHECK(BuildRoadCatchment({}).IsEmpty());

	for (uint x = 11; x <= 20; ++x) Road(x, 10);
	RoadCatchmentSource source{TileArea(TileXY(10, 10), 4, 1), 2, 6, std::nullopt};
	// Roads inside a planned facility will be removed, and cannot act as shortcuts.
	covered = BuildRoadCatchment(std::span(&source, 1), &source.area);
	CHECK(covered.Contains(TileXY(19, 11)));
	CHECK_FALSE(covered.Contains(TileXY(20, 11)));
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - station cache updates after edits and rail conversion", "[station-catchment]")
{
	Station *st = RailStation();
	for (uint x = 11; x <= 25; ++x) Road(x, 10);
	_settings_game.station.modified_catchment = true;
	st->RecomputeCatchment();
	CHECK(st->TileIsInCatchment(TileXY(14, 14))); // Legacy train radius 4, regardless of roads.
	CHECK_FALSE(st->TileIsInCatchment(TileXY(16, 11)));
	_settings_game.station.road_access_catchment = true;
	st->RecomputeCatchment();
	CHECK_FALSE(st->TileIsInCatchment(TileXY(14, 14)));
	CHECK(st->TileIsInCatchment(TileXY(16, 11)));
	uint64_t revision = GetStationRoadAccessRevision();
	SetRoadBits(TileXY(13, 10), ROAD_Y, RoadTramType::Road);
	CHECK(GetStationRoadAccessRevision() > revision);
	UpdateStationRoadAccess();
	CHECK_FALSE(st->TileIsInCatchment(TileXY(16, 11)));
	SetRoadBits(TileXY(13, 10), ROAD_X, RoadTramType::Road);
	UpdateStationRoadAccess();
	CHECK(st->TileIsInCatchment(TileXY(16, 11)));
	SetRailType(st->xy, RAILTYPE_MAGLEV);
	UpdateStationRoadAccess();
	CHECK(st->TileIsInCatchment(TileXY(19, 11)));
	CHECK_FALSE(st->TileIsInCatchment(TileXY(20, 11)));
	DoClearSquare(TileXY(13, 10));
	UpdateStationRoadAccess();
	CHECK_FALSE(st->TileIsInCatchment(TileXY(19, 11)));
	Road(13, 10);
	UpdateStationRoadAccess();
	CHECK(st->TileIsInCatchment(TileXY(19, 11)));
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - industrial cargo links beyond the old catchment", "[station-catchment]")
{
	REQUIRE(Station::CanAllocateItem());
	Station *st = Station::Create(TileXY(10, 10));
	st->facilities.Set(StationFacility::Airport);
	st->airport.type = AT_INTERCON;
	st->airport.Add(st->xy);
	st->spread = TileArea(st->xy, 1, 1);
	MakeAirport(st->xy, OWNER_NONE, st->index, 0, WaterClass::Invalid);
	for (uint x = 11; x <= 22; ++x) Road(x, 10);
	REQUIRE(Industry::CanAllocateItem());
	Industry *industry = Industry::Create(TileXY(22, 11));
	industry->location = TileArea(TileXY(22, 11), 1, 1);
	industry->accepted.emplace_back().cargo = CargoType{1}; // An industrial cargo, with no passenger/mail restriction.
	MakeIndustry(industry->location.tile, industry->index, 0, 0, WaterClass::Invalid);
	_settings_game.station.road_access_catchment = true;
	st->RecomputeCatchment();
	CHECK(industry->stations_near.contains(st));
	REQUIRE(st->industries_near.size() == 1);
	CHECK(st->industries_near.begin()->industry == industry);
	bool found = false;
	ForAllStationsAroundTiles(industry->location, [&](Station *near, TileIndex tile) {
		CHECK(near == st);
		CHECK(tile == industry->location.tile);
		found = true;
		return true;
	});
	CHECK(found);
	DoClearSquare(TileXY(13, 10));
	UpdateStationRoadAccess();
	CHECK_FALSE(industry->stations_near.contains(st));
	CHECK(st->industries_near.empty());
}

TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - coverage pins survive deselection and clear with stations", "[station-catchment]")
{
	Station *first = RailStation();
	Station *second = RailStation(30, 30);
	CHECK_FALSE(IsStationCoveragePinned(first));
	SetStationCoveragePinned(first, true);
	SetStationCoveragePinned(second, true);
	SetViewportCatchmentStation(first, true);
	SetViewportCatchmentStation(first, false); // Station window closes.
	CHECK(IsStationCoveragePinned(first));
	CHECK(IsStationCoveragePinned(second));
	SetStationCoveragePinned(first, false);
	CHECK_FALSE(IsStationCoveragePinned(first));
	CHECK(IsStationCoveragePinned(second));
	ClearPinnedStationCoverage();
	CHECK_FALSE(IsStationCoveragePinned(second));
	StationID id = first->index;
	SetStationCoveragePinned(first, true);
	_station_pool.CleanPool();
	Station *replacement = Station::CreateAtIndex(id, TileXY(10, 10));
	CHECK_FALSE(IsStationCoveragePinned(replacement));
}

TEST_CASE("Road catchment - setting defaults off and locks after game creation", "[station-catchment]")
{
	const SettingDesc *setting = GetSettingFromName("station.road_access_catchment");
	REQUIRE(setting != nullptr);
	CHECK(setting->AsIntSetting()->GetDefaultValue() == 0);
	CHECK(setting->AsIntSetting()->cat == SC_ADVANCED);
	CHECK_FALSE(setting->flags.Test(SettingFlag::NoNetworkSync));
	CHECK_FALSE(setting->flags.Test(SettingFlag::NotInSave));
	AutoRestoreBackup mode(_game_mode, GameMode::Menu);
	CHECK(setting->IsEditable());
	_game_mode = GameMode::Normal;
	CHECK_FALSE(setting->IsEditable());
	CHECK_FALSE(setting->IsEditable(true));
	_game_mode = GameMode::Editor;
	CHECK_FALSE(setting->IsEditable());
}


TEST_CASE_METHOD(RoadCatchmentFixture, "Road catchment - construction preview updates before painting", "[station-catchment]")
{
	AutoRestoreBackup highlight(_thd);
	AutoRestoreBackup show_coverage(_settings_client.gui.station_show_coverage, true);
	_settings_game.station.road_access_catchment = true;
	WindowDesc desc(WindowPosition::Manual, "", 0, 0, WindowClass::BuildStation, WindowClass::None, {}, {});
	auto close_window = [](Window *w) {
		ClearStationCoveragePreview();
		_thd.window_class = WindowClass::Invalid;
		w->Close();
		Window::DeleteClosedWindows();
	};
	std::unique_ptr<Window, decltype(close_window)> window(new Window(desc), close_window);
	window->window_class = WindowClass::BuildStation;
	_thd = {};
	_thd.window_class = WindowClass::BuildStation;
	_thd.drawstyle = HT_RECT;
	_thd.size = {TILE_SIZE, TILE_SIZE};
	_thd.pos = {10 * TILE_SIZE, 10 * TILE_SIZE};
	for (uint x = 11; x <= 20; ++x) Road(x, 10);
	auto source = MakeStationCoveragePreviewSource(StationType::Rail);
	CheckRedrawStationCoverage(window.get(), &source);
	REQUIRE(GetStationCoveragePreview() != nullptr);
	CHECK(GetStationCoveragePreview()->Contains(TileXY(16, 11)));

	// Moving must replace both the local area and road extension before any OnPaint call.
	_thd.pos = {30 * TILE_SIZE, 30 * TILE_SIZE};
	source = MakeStationCoveragePreviewSource(StationType::Rail);
	CheckRedrawStationCoverage(window.get(), &source);
	REQUIRE(GetStationCoveragePreview() != nullptr);
	CHECK_FALSE(GetStationCoveragePreview()->Contains(TileXY(10, 10)));
	CHECK_FALSE(GetStationCoveragePreview()->Contains(TileXY(16, 11)));
	CHECK(GetStationCoveragePreview()->Contains(TileXY(30, 30)));

	_settings_client.gui.station_show_coverage = false;
	CheckRedrawStationCoverage(window.get(), &source);
	CHECK(GetStationCoveragePreview() == nullptr);
	_settings_client.gui.station_show_coverage = true;
	CheckRedrawStationCoverage(window.get(), &source);
	REQUIRE(GetStationCoveragePreview() != nullptr);
	CHECK(GetStationCoveragePreview()->Contains(TileXY(30, 30)));

	_thd.pos = {10 * TILE_SIZE, 10 * TILE_SIZE};
	source = MakeStationCoveragePreviewSource(StationType::Rail);
	CheckRedrawStationCoverage(window.get(), &source);
	DoClearSquare(TileXY(13, 10));
	CheckRedrawStationCoverage(window.get(), &source);
	CHECK_FALSE(GetStationCoveragePreview()->Contains(TileXY(16, 11)));
	_thd.drawstyle = HT_NONE; // Leaving the viewport or cancelling construction clears the preview.
	CheckRedrawStationCoverage(window.get(), &source);
	CHECK(GetStationCoveragePreview() == nullptr);
}
