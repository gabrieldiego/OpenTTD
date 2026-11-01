/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file aircraft_range.cpp Tests for aircraft range geometry, airport references, and overlay cleanup. */

#include "../stdafx.h"
#include "../3rdparty/catch2/catch.hpp"
#include "mock_environment.h"
#include "../aircraft.h"
#include "../aircraft_range.h"
#include "../station_base.h"
#include "../clear_map.h"
#include "../newgrf_airport.h"
#include "../spritecache.h"
#include "../palette_func.h"
#include "../table/sprites.h"

#include "../safeguards.h"

class AircraftRangeFixture {
public:
	AircraftRangeFixture()
	{
		Map::Allocate(64, 64);
		for (TileIndex tile{0}; tile < Map::Size(); ++tile) MakeClear(tile, ClearGround::Grass, 3);
		AirportSpec::ResetAirports();
	}

	~AircraftRangeFixture()
	{
		_vehicle_pool.CleanPool();
		UpdateAircraftRangeHighlight();
		_station_pool.CleanPool();
	}

	static Station *Airport(uint x = 10, uint y = 10)
	{
		REQUIRE(Station::CanAllocateItem());
		Station *st = Station::Create(TileXY(x, y));
		st->airport.type = AT_SMALL;
		st->airport.rotation = Direction::N;
		st->airport.Add(st->xy);
		st->facilities.Set(StationFacility::Airport);
		st->spread = TileArea(st->xy, 4, 3);
		for (TileIndex tile : st->spread) MakeAirport(tile, OWNER_NONE, st->index, 0, WaterClass::Invalid);
		return st;
	}

	static Aircraft *Plane(const Station &st)
	{
		REQUIRE(Aircraft::CanAllocateItem());
		Aircraft *v = Aircraft::Create();
		v->subtype = AIR_AIRCRAFT;
		v->tile = st.airport.GetHangarTile(0);
		v->vehstatus.Set({VehState::Hidden, VehState::Stopped});
		v->acache.cached_max_range = 5;
		return v;
	}
};

TEST_CASE_METHOD(AircraftRangeFixture, "Aircraft range - circular boundary and unlimited range", "[aircraft-range]")
{
	AircraftRange range{TileXY(10, 10), 5};
	CHECK(range.Contains(TileXY(10, 10)));
	CHECK(range.Contains(TileXY(15, 10)));
	CHECK(range.Contains(TileXY(13, 14)));
	CHECK_FALSE(range.Contains(TileXY(16, 10)));
	CHECK_FALSE(range.Contains(TileXY(14, 14)));
	CHECK_FALSE(range.Contains(INVALID_TILE));
	range.range = 0;
	CHECK(range.Contains(TileXY(63, 63)));
	CHECK_FALSE(range.Contains(INVALID_TILE));
	range.range = UINT16_MAX;
	CHECK(range.Contains(TileXY(63, 63))); // Squaring the maximum range must not overflow signed int.
}

TEST_CASE_METHOD(AircraftRangeFixture, "Aircraft range - airport reference instead of hangar or footprint", "[aircraft-range]")
{
	Station *origin = Airport();
	Aircraft *v = Plane(*origin);
	auto range = GetAircraftRange(v);
	REQUIRE(range.has_value());
	CHECK(range->origin == origin->airport.tile);
	CHECK(range->ContainsAirport(*origin));
	Station *edge = Airport(15, 10);
	CHECK(range->ContainsAirport(*edge));
	CHECK_FALSE(range->Contains(TileXY(18, 10))); // The whole airport is reachable by its reference tile.
	edge->airport.tile = TileXY(16, 10);
	CHECK_FALSE(range->ContainsAirport(*edge));
	edge->airport.Clear();
	CHECK_FALSE(range->ContainsAirport(*edge));
}

TEST_CASE_METHOD(AircraftRangeFixture, "Aircraft range - only available for stopped aircraft in hangars", "[aircraft-range]")
{
	Station *st = Airport();
	Aircraft *v = Plane(*st);
	CHECK(GetAircraftRange(v).has_value());
	v->vehstatus.Reset(VehState::Stopped);
	CHECK_FALSE(GetAircraftRange(v).has_value());
	v->vehstatus.Set(VehState::Stopped);
	v->vehstatus.Reset(VehState::Hidden);
	CHECK_FALSE(GetAircraftRange(v).has_value());
	v->vehstatus.Set({VehState::Hidden, VehState::Crashed});
	CHECK_FALSE(GetAircraftRange(v).has_value());
	v->vehstatus.Reset(VehState::Crashed);
	v->tile = TileXY(1, 1);
	CHECK_FALSE(GetAircraftRange(v).has_value());
	CHECK_FALSE(GetAircraftRange(nullptr).has_value());
}

TEST_CASE_METHOD(AircraftRangeFixture, "Aircraft range - switching, dismissal, departure and deleted vehicles", "[aircraft-range]")
{
	Station *st = Airport();
	Aircraft *first = Plane(*st), *second = Plane(*st);
	SetAircraftRangeHighlight(first->index, true);
	CHECK(IsAircraftRangeHighlighted(first->index));
	REQUIRE(GetAircraftRangeHighlight() != nullptr);
	CHECK(GetAircraftRangeHighlight()->range == 5);
	SetAircraftRangeHighlight(second->index, true);
	CHECK_FALSE(IsAircraftRangeHighlighted(first->index));
	SetAircraftRangeHighlight(first->index, false); // Closing another aircraft window must not clear it.
	CHECK(IsAircraftRangeHighlighted(second->index));
	second->acache.cached_max_range = 7;
	UpdateAircraftRangeHighlight();
	CHECK(GetAircraftRangeHighlight()->range == 7);
	second->vehstatus.Reset(VehState::Stopped);
	UpdateAircraftRangeHighlight();
	CHECK(GetAircraftRangeHighlight() == nullptr);
	SetAircraftRangeHighlight(first->index, true);
	SetAircraftRangeHighlight(first->index, false);
	CHECK(GetAircraftRangeHighlight() == nullptr);
	SetAircraftRangeHighlight(first->index, true);
	_vehicle_pool.CleanPool();
	UpdateAircraftRangeHighlight();
	CHECK(GetAircraftRangeHighlight() == nullptr);
}

TEST_CASE("Aircraft range - built-in selection palette supports sprite cache queries", "[aircraft-range]")
{
	MockEnvironment::Instance();
	CHECK(SpriteExists(PALETTE_SEL_TILE_LIGHT_BLUE));
	CHECK(GetMaxSpriteID() > PALETTE_SEL_TILE_LIGHT_BLUE);
	CHECK(GetSpriteType(PALETTE_SEL_TILE_LIGHT_BLUE) == SpriteType::Recolour);
	CHECK(GetOriginFile(PALETTE_SEL_TILE_LIGHT_BLUE) == nullptr);
	CHECK(GetSpriteLocalID(PALETTE_SEL_TILE_LIGHT_BLUE) == 0);
	const uint8_t *palette = GetNonSprite(PALETTE_SEL_TILE_LIGHT_BLUE, SpriteType::Recolour);
	CHECK(palette[1] == 0);
	CHECK(palette[9 + 1] != PC_LIGHT_BLUE.p);
	CHECK(palette[13 + 1] != palette[9 + 1]);
	CHECK(palette[23 + 1] == 0);
	UniquePtrSpriteAllocator allocator;
	const auto *copy = static_cast<const uint8_t *>(GetRawSprite(PALETTE_SEL_TILE_LIGHT_BLUE, SpriteType::Recolour, &allocator));
	REQUIRE(copy != nullptr);
	CHECK(copy != palette);
	CHECK(allocator.size == 257);
	CHECK(std::equal(copy, copy + 257, palette));
}
