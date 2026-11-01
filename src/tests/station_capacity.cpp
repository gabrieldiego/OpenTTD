/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_capacity.cpp Tests for station storage, cargo patience, and loading boundaries. */

#include "../stdafx.h"
#include "../3rdparty/catch2/catch.hpp"
#include "../clear_map.h"
#include "../core/backup_type.hpp"
#include "../cargotype.h"
#include "../economy_base.h"
#include "../newgrf_airport.h"
#include "../rail.h"
#include "../road.h"
#include "../station_base.h"
#include "../settings_internal.h"
#include "../station_capacity.h"
#include "../station_map.h"

#include "../safeguards.h"

class StationCapacityFixture {
	AutoRestoreBackup<StationSettings> settings{_settings_game.station};
	std::array<CargoClasses, NUM_CARGO> classes{};

public:
	StationCapacityFixture()
	{
		_settings_game.station.cargo_patience = true;
		for (CargoType cargo : EnumRange(NUM_CARGO)) {
			this->classes[cargo] = CargoSpec::Get(cargo)->classes;
			CargoSpec::Get(cargo)->classes = {};
		}
		CargoSpec::Get(0)->classes.Set(CargoClass::Passengers);
		CargoSpec::Get(1)->classes.Set(CargoClass::Mail);
		CargoSpec::Get(3)->classes.Set(CargoClass::Express);
		CargoSpec::Get(4)->classes.Set(CargoClass::Refrigerated);
		Map::Allocate(64, 64);
		for (TileIndex tile{0}; tile < Map::Size(); ++tile) MakeClear(tile, ClearGround::Grass, 3);
		ResetRoadTypes();
		ResetRailTypes();
		AirportSpec::ResetAirports();
	}

	~StationCapacityFixture()
	{
		_cargo_payment_pool.CleanPool();
		_station_pool.CleanPool();
		_cargopacket_pool.CleanPool();
		for (CargoType cargo : EnumRange(NUM_CARGO)) CargoSpec::Get(cargo)->classes = this->classes[cargo];
	}

	static Station *RailStation(RailType type = RAILTYPE_RAIL)
	{
		REQUIRE(Station::CanAllocateItem());
		Station *st = Station::Create(TileXY(10, 10));
		st->spread = st->train_station = TileArea(st->xy, 1, 1);
		st->facilities.Set(StationFacility::Train);
		MakeRailStation(st->xy, OWNER_NONE, st->index, Axis::X, 0, type);
		return st;
	}

	static CargoPacket *Packet(uint16_t count)
	{
		REQUIRE(CargoPacket::CanAllocateItem());
		return CargoPacket::Create(count, 0, StationID::Invalid(), TileXY(1, 1), Money{});
	}

	static CargoPacket *Add(Station &st, CargoType cargo, uint16_t count)
	{
		CargoPacket *cp = Packet(count);
		st.goods[cargo].GetOrCreateData().cargo.Append(cp, StationID::Invalid());
		return cp;
	}
};

TEST_CASE_METHOD(StationCapacityFixture, "Station capacity - railway technology and actual joined tiles", "[station-capacity]")
{
	Station *st = RailStation();
	std::array<RailType, 4> types{RAILTYPE_RAIL, RAILTYPE_ELECTRIC, RAILTYPE_MONO, RAILTYPE_MAGLEV};
	std::array<uint, 4> passengers{256, 384, 512, 768};
	std::array<uint, 4> mail{192, 256, 320, 384};
	for (uint i = 0; i < types.size(); ++i) {
		SetRailType(st->xy, types[i]);
		auto capacity = GetStationCargoCapacities(*st);
		CHECK(capacity[StationCargoPool::Passengers] == passengers[i]);
		CHECK(capacity[StationCargoPool::Mail] == mail[i]);
		CHECK(capacity[StationCargoPool::Freight] == passengers[i]);
	}
	SetRailType(st->xy, RAILTYPE_RAIL);
	st->spread = TileArea(st->xy, 5, 2);
	MakeRoadStop(TileXY(12, 10), OWNER_NONE, st->index, RoadStopType::Bus, ROADTYPE_ROAD, INVALID_ROADTYPE, DiagDirection::NE);
	MakeRoadStop(TileXY(14, 10), OWNER_NONE, st->index, RoadStopType::Truck, ROADTYPE_ROAD, INVALID_ROADTYPE, DiagDirection::NE);
	MakeRailStation(TileXY(11, 11), OWNER_NONE, StationID{10}, Axis::X, 0, RAILTYPE_MAGLEV);
	auto capacity = GetStationCargoCapacities(*st);
	CHECK(capacity[StationCargoPool::Passengers] == 256 + 128);
	CHECK(capacity[StationCargoPool::Mail] == 192 + 128);
	CHECK(capacity[StationCargoPool::Freight] == 256 + 512);
}

TEST_CASE_METHOD(StationCapacityFixture, "Station capacity - airport categories counted once and additive dock", "[station-capacity]")
{
	Station *st = RailStation();
	st->spread = TileArea(st->xy, 4, 4);
	MakeAirport(TileXY(11, 10), OWNER_NONE, st->index, 0, WaterClass::Invalid);
	MakeAirport(TileXY(12, 10), OWNER_NONE, st->index, 0, WaterClass::Invalid);
	std::array<std::array<uint, 4>, 9> cases{{
		{AT_SMALL, 512, 768, 64}, {AT_COMMUTER, 1024, 384, 96}, {AT_LARGE, 2048, 1024, 256},
		{AT_METROPOLITAN, 4096, 1024, 512}, {AT_INTERNATIONAL, 8192, 2048, 1024}, {AT_INTERCON, 16384, 2048, 2048},
		{AT_HELIPORT, 64, 32, 4}, {AT_HELIDEPOT, 128, 64, 8}, {AT_HELISTATION, 256, 128, 16},
	}};
	for (auto c : cases) {
		st->airport.type = c[0];
		auto capacity = GetStationCargoCapacities(*st);
		CHECK(capacity[StationCargoPool::Passengers] == 256 + c[1]);
		CHECK(capacity[StationCargoPool::Mail] == 192 + c[2]);
		CHECK(capacity[StationCargoPool::Freight] == 256 + c[3]);
	}
	st->airport.type = AT_SMALL;
	MakeDock(TileXY(11, 12), OWNER_NONE, st->index, DiagDirection::SE, WaterClass::Sea);
	auto capacity = GetStationCargoCapacities(*st);
	CHECK(capacity[StationCargoPool::Passengers] == 256 + 512 + 512);
	CHECK(capacity[StationCargoPool::Mail] == 192 + 768 + 256);
	CHECK(capacity[StationCargoPool::Freight] == 256 + 64 + 8192);
}

TEST_CASE_METHOD(StationCapacityFixture, "Station capacity - separate passengers and mail, shared freight", "[station-capacity]")
{
	Station *st = RailStation();
	Add(*st, CargoType{0}, 100);
	Add(*st, CargoType{1}, 70);
	Add(*st, CargoType{2}, 180);
	Add(*st, CargoType{3}, 60);
	auto usage = GetStationCargoUsage(*st);
	CHECK(usage[StationCargoPool::Passengers] == 100);
	CHECK(usage[StationCargoPool::Mail] == 70);
	CHECK(usage[StationCargoPool::Freight] == 240);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{0}) == 156);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{1}) == 122);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{2}) == 16);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{3}) == 16);
	Add(*st, CargoType{2}, 16);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{3}) == 0);
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - crowding curve and cargo classes", "[station-capacity]")
{
	for (auto [cargo, base] : {std::pair{0, 1000}, {1, 750}, {2, 250}, {3, 500}, {4, 1250}}) {
		const auto &cs = *CargoSpec::Get(cargo);
		CHECK(GetCargoPatienceDecay(cs, 0, 100) == base);
		CHECK(GetCargoPatienceDecay(cs, 50, 100) == base * 7 / 4);
		CHECK(GetCargoPatienceDecay(cs, 100, 100) == base * 4);
		CHECK(GetCargoPatienceDecay(cs, 200, 100) == base * 4);
		CHECK(GetCargoPatienceDecay(cs, 0, 0) == base * 4);
		CHECK(GetCargoPatienceDecay(cs, UINT_MAX, UINT_MAX) == base * 4);
	}
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - expiry, weighted ratings and empty station", "[station-capacity]")
{
	Station *st = RailStation();
	CHECK(GetStationOverallPatienceRating(*st) == 255);
	CHECK(st->goods[0].rating == 255);
	auto &list = st->goods[0].GetOrCreateData().cargo;
	CargoPacket *old = Add(*st, CargoType{0}, 10);
	CHECK(list.AgePatience(30000) == 0);
	CHECK(old->GetStationWaitingPeriods() == 1);
	CargoPacket *fresh = Add(*st, CargoType{0}, 30);
	CHECK(list.Packets()->size() == 2);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(st->goods[0].rating == 223); // 10 at 50%, 30 at 100% = 87.5%.
	CHECK(GetStationOverallPatienceRating(*st) == 223);
	CHECK(list.AgePatience(30000) == 10);
	CHECK(list.AvailableCount() == 30);
	CHECK(fresh->GetStationPatience() == 30000);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(st->goods[0].rating == 128);
	CHECK(list.AgePatience(30000) == 30);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(st->goods[0].rating == 255);
	CHECK(GetStationOverallPatienceRating(*st) == 255);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{0}) == 256);
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - packet splits and transfers preserve remaining patience", "[station-capacity]")
{
	StationCargoList source, dest;
	CargoPacket *cp = Packet(100);
	source.Append(cp, StationID::Invalid());
	source.AgePatience(10000);
	VehicleCargoList vehicle;
	std::array<StationID, 1> next{StationID::Invalid()};
	CHECK(source.Load(100, &vehicle, next, TileXY(10, 10)) == 100);
	REQUIRE(CargoPayment::CanAllocateItem());
	CargoPayment *payment = CargoPayment::Create();
	GoodsEntry ge;
	CHECK(vehicle.Stage(false, StationID{1}, {}, OrderUnloadType::Transfer, &ge, CargoType{0}, payment, TileXY(10, 10)));
	CHECK(vehicle.Unload(100, &dest, CargoType{0}, nullptr, TileXY(20, 20), 0) == 0);
	CHECK(vehicle.StoredCount() == 100);
	CHECK(dest.AvailableCount() == 0);
	CHECK(vehicle.Unload(100, &dest, CargoType{0}, nullptr, TileXY(20, 20), 30) == 30);
	CHECK(vehicle.StoredCount() == 70);
	CHECK(dest.AvailableCount() == 30);
	auto packets = dest.Packets();
	const CargoPacket *split = *StationCargoList::ConstIterator(packets->begin());
	CHECK(split->GetStationPatience() == 50000);
	CHECK(split->GetStationWaitingPeriods() == 1);
	CHECK(vehicle.Unload(100, &dest, CargoType{0}, nullptr, TileXY(20, 20), 70) == 70);
	CHECK(vehicle.StoredCount() == 0);
	CHECK(dest.AvailableCount() == 100);
	CHECK(dest.Packets()->size() == 1);
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - boarding reservations occupy storage and preserve age", "[station-capacity]")
{
	Station *st = RailStation();
	Add(*st, CargoType{0}, 200);
	auto &list = st->goods[0].GetData().cargo;
	list.AgePatience(10000);
	VehicleCargoList vehicle;
	std::array<StationID, 1> next{StationID::Invalid()};
	CHECK(list.Reserve(100, &vehicle, next, st->xy) == 100);
	CHECK(list.AvailableCount() == 100);
	CHECK(list.ReservedCount() == 100);
	CHECK(GetStationCargoUsage(*st)[StationCargoPool::Passengers] == 200);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{0}) == 56);
	CHECK(vehicle.ReservedPatienceSum() == 5000000);
	CHECK(list.AgePatience(50000) == 100);
	CHECK(list.TotalCount() == 100);
	CHECK(vehicle.Return(100, &list, StationID::Invalid(), st->xy) == 100);
	CHECK(list.ReservedCount() == 0);
	CHECK(list.TotalCount() == 100);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(st->goods[0].rating == 213);
	CHECK(list.Load(100, &vehicle, next, st->xy) == 100);
	CHECK(list.TotalCount() == 0);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(GetStationOverallPatienceRating(*st) == 255);
}

TEST_CASE_METHOD(StationCapacityFixture, "Station capacity - downgrade discards oldest freight across cargo types", "[station-capacity]")
{
	Station *st = RailStation(RAILTYPE_MAGLEV);
	Add(*st, CargoType{2}, 200);
	auto &older = st->goods[2].GetData().cargo;
	older.AgePatience(250);
	Add(*st, CargoType{3}, 200);
	CHECK(EnforceStationCargoCapacity(*st) == 0);
	SetRailType(st->xy, RAILTYPE_RAIL);
	CHECK(EnforceStationCargoCapacity(*st) == 144);
	CHECK(st->goods[2].TotalCount() == 56);
	CHECK(st->goods[3].TotalCount() == 200);
	CHECK(GetStationCargoFreeSpace(*st, CargoType{2}) == 0);
	MakeClear(st->xy, ClearGround::Grass, 3);
	CHECK(EnforceStationCargoCapacity(*st) == 256);
	CHECK(GetStationCargoUsage(*st)[StationCargoPool::Freight] == 0);
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - pool occupancy drives update, legacy mode unchanged", "[station-capacity]")
{
	Station *st = RailStation();
	CargoPacket *passengers = Add(*st, CargoType{0}, 1);
	CargoPacket *coal = Add(*st, CargoType{2}, 128);
	CargoPacket *goods = Add(*st, CargoType{3}, 128);
	UpdateStationCargoPatience(*st);
	CHECK(passengers->GetStationPatience() == 59000);
	CHECK(coal->GetStationPatience() == 59000);
	CHECK(goods->GetStationPatience() == 58000);
	_settings_game.station.cargo_patience = false;
	Add(*st, CargoType{2}, 500);
	uint before = st->goods[2].TotalCount();
	UpdateStationCargoPatience(*st);
	CHECK(EnforceStationCargoCapacity(*st) == 0);
	CHECK(st->goods[2].TotalCount() == before);
	CHECK(passengers->GetStationWaitingPeriods() == 1);
}

TEST_CASE("Cargo patience - opt-in setting is locked after world creation", "[station-capacity]")
{
	AutoRestoreBackup mode(_game_mode, GameMode::Menu);
	const auto *setting = GetSettingFromName("station.cargo_patience");
	REQUIRE(setting != nullptr);
	CHECK(setting->IsEditable());
	CHECK(setting->AsIntSetting()->GetDefaultValue() == 0);
	CHECK(setting->AsIntSetting()->cat == SC_ADVANCED);
	CHECK_FALSE(setting->flags.Test(SettingFlag::NoNetworkSync));
	CHECK_FALSE(setting->flags.Test(SettingFlag::NotInSave));
	CHECK(StationSettings{}.cargo_patience == false);
	_game_mode = GameMode::Normal;
	CHECK_FALSE(setting->IsEditable());
	_game_mode = GameMode::Editor;
	CHECK_FALSE(setting->IsEditable());
}

TEST_CASE_METHOD(StationCapacityFixture, "Station capacity - shrinking protects boarding until loaded or returned", "[station-capacity]")
{
	Station *st = RailStation();
	Add(*st, CargoType{0}, 200);
	auto &list = st->goods[0].GetData().cargo;
	VehicleCargoList vehicle;
	std::array<StationID, 1> next{StationID::Invalid()};
	CHECK(list.Reserve(100, &vehicle, next, st->xy) == 100);
	MakeClear(st->xy, ClearGround::Grass, 3);
	CHECK(EnforceStationCargoCapacity(*st) == 100);
	CHECK(list.ReservedCount() == 100);
	CHECK(list.AvailableCount() == 0);
	CHECK(EnforceStationCargoCapacity(*st) == 0);
	CHECK(vehicle.Return(100, &list, StationID::Invalid(), st->xy) == 100);
	CHECK(EnforceStationCargoCapacity(*st) == 100);
	CHECK(list.TotalCount() == 0);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(GetStationOverallPatienceRating(*st) == 255);
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - overall rating weights units across cargo types", "[station-capacity]")
{
	Station *st = RailStation();
	Add(*st, CargoType{0}, 1);
	st->goods[0].GetData().cargo.AgePatience(30000);
	Add(*st, CargoType{1}, 99);
	RefreshStationCargoPatienceRatings(*st);
	CHECK(st->goods[0].rating == 128);
	CHECK(st->goods[1].rating == 255);
	CHECK(GetStationOverallPatienceRating(*st) == 254);
}

TEST_CASE_METHOD(StationCapacityFixture, "Cargo patience - expiry and partial eviction preserve cargo caches", "[station-capacity]")
{
	StationCargoList list;
	REQUIRE(CargoPacket::CanAllocateItem());
	CargoPacket *old = CargoPacket::Create(uint16_t{10}, uint16_t{3}, StationID::Invalid(), TileXY(1, 1), Money{});
	list.Append(old, StationID::Invalid());
	list.AgePatience(30000);
	REQUIRE(CargoPacket::CanAllocateItem());
	CargoPacket *fresh = CargoPacket::Create(uint16_t{20}, uint16_t{7}, StationID::Invalid(), TileXY(1, 1), Money{});
	list.Append(fresh, StationID::Invalid());
	CHECK(list.RemovePacket(old, 5) == 5);
	CHECK(list.AvailableCount() == 25);
	CHECK(list.PeriodsInTransit() == 6);
	CHECK(list.AgePatience(30000) == 5);
	CHECK(list.AvailableCount() == 20);
	CHECK(list.PeriodsInTransit() == 7);
	CHECK(list.RemovePacket(fresh, 7) == 7);
	CHECK(list.AvailableCount() == 13);
	CHECK(list.PeriodsInTransit() == 7);
	CHECK(list.AgePatience(30000) == 13);
	CHECK(list.AvailableCount() == 0);
	CHECK(list.PeriodsInTransit() == 0);
}
