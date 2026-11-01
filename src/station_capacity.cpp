/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_capacity.cpp Storage capacities and patience-based station ratings. */

#include "stdafx.h"
#include "station_capacity.h"
#include "station_base.h"
#include "station_map.h"
#include "rail.h"
#include "newgrf_airport.h"
#include "cargotype.h"
#include "vehicle_base.h"
#include "window_func.h"

#include "safeguards.h"

StationCargoPool GetStationCargoPool(const CargoSpec &cs)
{
	if (cs.classes.Test(CargoClass::Passengers)) return StationCargoPool::Passengers;
	if (cs.classes.Test(CargoClass::Mail)) return StationCargoPool::Mail;
	return StationCargoPool::Freight;
}

static StationCargoAmounts Capacity(uint passengers, uint mail, uint freight)
{
	StationCargoAmounts result{};
	result[StationCargoPool::Passengers] = passengers;
	result[StationCargoPool::Mail] = mail;
	result[StationCargoPool::Freight] = freight;
	return result;
}

/** Capacity per airport, not per airport tile. NewGRFs inherit their substitute airport's category. */
static StationCargoAmounts AirportCapacity(const Airport &airport)
{
	uint8_t type = airport.type;
	if (type >= NEW_AIRPORT_OFFSET) type = airport.GetSpec()->grf_prop.subst_id;
	switch (type) {
		case AT_SMALL:         return Capacity(512, 768, 64);
		case AT_COMMUTER:      return Capacity(1024, 384, 96);
		case AT_LARGE:         return Capacity(2048, 1024, 256);
		case AT_METROPOLITAN:  return Capacity(4096, 1024, 512);
		case AT_INTERNATIONAL: return Capacity(8192, 2048, 1024);
		case AT_INTERCON:      return Capacity(16384, 2048, 2048);
		case AT_HELIPORT:      return Capacity(64, 32, 4);
		case AT_HELIDEPOT:     return Capacity(128, 64, 8);
		case AT_HELISTATION:   return Capacity(256, 128, 16);
		default:              return Capacity(2048, 1024, 256);
	}
}

/** Capacities add across actual facilities, ignoring holes in a joined station's bounding rectangle. */
StationCargoAmounts GetStationCargoCapacities(const Station &st)
{
	StationCargoAmounts result{};
	bool airport_counted = false;
	for (TileIndex tile : st.spread) {
		if (!IsTileType(tile, TileType::Station) || GetStationIndex(tile) != st.index) continue;
		StationCargoAmounts part{};
		switch (GetStationType(tile)) {
			case StationType::Rail: {
				RailTypeLabel label = GetRailTypeInfo(GetRailType(tile))->label;
				if (label == RAILTYPE_LABEL_MAGLEV) part = Capacity(768, 384, 768);
				else if (label == RAILTYPE_LABEL_MONO) part = Capacity(512, 320, 512);
				else if (label == RAILTYPE_LABEL_ELECTRIC) part = Capacity(384, 256, 384);
				else part = Capacity(256, 192, 256);
				break;
			}
			case StationType::Bus: part = Capacity(128, 0, 0); break;
			case StationType::Truck: part = Capacity(0, 128, 512); break;
			case StationType::Dock:
				if (!IsDockWaterPart(tile)) part = Capacity(512, 256, 8192);
				break;
			case StationType::Oilrig: part = Capacity(512, 256, 8192); break;
			case StationType::Airport:
				if (!airport_counted) part = AirportCapacity(st.airport);
				airport_counted = true;
				break;
			default: break;
		}
		for (StationCargoPool pool : EnumRange(StationCargoPool::End)) result[pool] += part[pool];
	}
	return result;
}

StationCargoAmounts GetStationCargoUsage(const Station &st)
{
	StationCargoAmounts result{};
	for (CargoType cargo : EnumRange(NUM_CARGO)) result[GetStationCargoPool(*CargoSpec::Get(cargo))] += st.goods[cargo].TotalCount();
	return result;
}

uint GetStationCargoFreeSpace(const Station &st, CargoType cargo)
{
	StationCargoPool pool = GetStationCargoPool(*CargoSpec::Get(cargo));
	uint capacity = GetStationCargoCapacities(st)[pool];
	uint used = GetStationCargoUsage(st)[pool];
	return capacity > used ? capacity - used : 0;
}

/**
 * One update is 2.5 simulation days. Uncrowded patience lasts 150 days for passengers,
 * 200 for mail, 120 for refrigerated freight, 300 for express freight and 600 for other freight.
 * A full pool consumes patience four times as quickly; easing congestion never restores spent patience.
 */
uint16_t GetCargoPatienceDecay(const CargoSpec &cs, uint used, uint capacity)
{
	uint base = cs.classes.Test(CargoClass::Passengers) ? 1000 :
			cs.classes.Test(CargoClass::Mail) ? 750 :
			cs.classes.Test(CargoClass::Refrigerated) ? 1250 :
			cs.classes.Test(CargoClass::Express) ? 500 : 250;
	uint occupancy = capacity == 0 ? 1000 : static_cast<uint>(std::min<uint64_t>(1000, static_cast<uint64_t>(used) * 1000 / capacity));
	return static_cast<uint16_t>(base + static_cast<uint64_t>(base) * 3 * occupancy * occupancy / 1000000);
}

struct PatienceTotal {
	uint count = 0;
	uint64_t patience = 0;
};
using CargoPatienceTotals = std::array<PatienceTotal, NUM_CARGO>;

/** Include reserved cargo: it still uses storage, but is protected from abandonment while boarding. */
static CargoPatienceTotals GetCargoPatienceTotals(const Station &st)
{
	CargoPatienceTotals result{};
	std::array<uint, NUM_CARGO> reserved{};
	for (CargoType cargo : EnumRange(NUM_CARGO)) {
		const GoodsEntry &ge = st.goods[cargo];
		result[cargo].count = ge.TotalCount();
		if (!ge.HasData()) continue;
		const auto *packets = ge.GetData().cargo.Packets();
		for (StationCargoList::ConstIterator it(packets->begin()); it != packets->end(); ++it) result[cargo].patience += static_cast<uint64_t>((*it)->Count()) * (*it)->GetStationPatience();
		reserved[cargo] = ge.GetData().cargo.ReservedCount();
	}
	for (const Vehicle *front : st.loading_vehicles) {
		for (const Vehicle *v = front; v != nullptr; v = v->Next()) {
			if (!IsValidCargoType(v->cargo_type)) continue;
			uint count = v->cargo.ReservedCount();
			assert(count <= reserved[v->cargo_type]);
			if (count == 0) continue;
			result[v->cargo_type].patience += v->cargo.ReservedPatienceSum();
			reserved[v->cargo_type] -= count;
		}
	}
	// During loading/conversion, reservations may not yet have an associated loading vehicle.
	for (CargoType cargo : EnumRange(NUM_CARGO)) result[cargo].patience += static_cast<uint64_t>(reserved[cargo]) * CargoPacket::MAX_STATION_PATIENCE;
	return result;
}

static uint8_t PatienceRating(uint64_t patience, uint64_t count)
{
	if (count == 0) return MAX_STATION_RATING;
	uint64_t denominator = count * CargoPacket::MAX_STATION_PATIENCE;
	return static_cast<uint8_t>((patience * MAX_STATION_RATING + denominator / 2) / denominator);
}

uint8_t GetStationOverallPatienceRating(const Station &st)
{
	uint64_t count = 0, patience = 0;
	for (const auto &total : GetCargoPatienceTotals(st)) {
		count += total.count;
		patience += total.patience;
	}
	return PatienceRating(patience, count);
}

void RefreshStationCargoPatienceRatings(Station &st)
{
	if (!_settings_game.station.cargo_patience) return;
	auto totals = GetCargoPatienceTotals(st);
	for (CargoType cargo : EnumRange(NUM_CARGO)) st.goods[cargo].rating = PatienceRating(totals[cargo].patience, totals[cargo].count);
}

/** Shrinking facilities releases the oldest available units first. Cargo already boarding is protected. */
uint EnforceStationCargoCapacity(Station &st)
{
	if (!_settings_game.station.cargo_patience) return 0;
	auto capacity = GetStationCargoCapacities(st);
	auto usage = GetStationCargoUsage(st);
	uint removed = 0;
	for (StationCargoPool pool : EnumRange(StationCargoPool::End)) {
		while (usage[pool] > capacity[pool]) {
			CargoPacket *oldest = nullptr;
			StationCargoList *list = nullptr;
			for (CargoType cargo : EnumRange(NUM_CARGO)) {
				GoodsEntry &ge = st.goods[cargo];
				if (!ge.HasData() || GetStationCargoPool(*CargoSpec::Get(cargo)) != pool) continue;
				const auto *packets = ge.GetData().cargo.Packets();
				for (StationCargoList::ConstIterator it(packets->begin()); it != packets->end(); ++it) {
					CargoPacket *cp = *it;
					if (oldest == nullptr || cp->GetStationWaitingPeriods() > oldest->GetStationWaitingPeriods() ||
							(cp->GetStationWaitingPeriods() == oldest->GetStationWaitingPeriods() && cp->index < oldest->index)) {
						oldest = cp;
						list = &ge.GetData().cargo;
					}
				}
			}
			if (oldest == nullptr) break;
			uint count = list->RemovePacket(oldest, usage[pool] - capacity[pool]);
			removed += count;
			usage[pool] -= count;
		}
	}
	return removed;
}

/** Replaces frequency/speed bonuses and random truncation when the optional mode is enabled. */
void UpdateStationCargoPatience(Station &st)
{
	if (!_settings_game.station.cargo_patience) return;
	auto capacity = GetStationCargoCapacities(st);
	auto usage = GetStationCargoUsage(st);
	uint removed = 0;
	for (CargoType cargo : EnumRange(NUM_CARGO)) {
		GoodsEntry &ge = st.goods[cargo];
		if (!ge.HasData()) continue;
		const CargoSpec &cs = *CargoSpec::Get(cargo);
		StationCargoPool pool = GetStationCargoPool(cs);
		removed += ge.GetData().cargo.AgePatience(GetCargoPatienceDecay(cs, usage[pool], capacity[pool]));
	}
	removed += EnforceStationCargoCapacity(st);
	RefreshStationCargoPatienceRatings(st);
	if (removed != 0) st.MarkTilesDirty(true);
	SetWindowDirty(WindowClass::StationView, st.index);
	InvalidateWindowData(WindowClass::StationList, st.owner);
}
