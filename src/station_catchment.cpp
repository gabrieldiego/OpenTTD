/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_catchment.cpp Road-access station catchment calculation. */

#include "stdafx.h"
#include "station_catchment.h"
#include "station_base.h"
#include "station_func.h"
#include "road_map.h"
#include "rail.h"
#include "tunnelbridge_map.h"
#include "newgrf_airport.h"
#include "gfx_func.h"
#include <queue>

#include "safeguards.h"

static FlatSet<StationID> _dirty_road_catchments;
static uint64_t _road_access_revision = 0;

RoadCatchmentSource MakeRoadCatchmentSource(const TileArea &area, StationType type, uint airport_catchment, RailType railtype, std::optional<DiagDirection> dock_direction)
{
	uint distance = 0;
	switch (type) {
		case StationType::Rail: {
			RailTypeLabel label = GetRailTypeInfo(railtype)->label;
			distance = label == RAILTYPE_LABEL_MAGLEV ? 9 : label == RAILTYPE_LABEL_MONO ? 7 : 6;
			break;
		}
		case StationType::Airport: distance = airport_catchment + 2; break;
		case StationType::Dock: distance = CA_DOCK + 2; break;
		default: break;
	}
	uint local = type == StationType::Bus || type == StationType::Truck ? 4 : 2;
	return {area, local, distance, dock_direction};
}

/** Bounding radius includes buildings one tile beside the furthest reachable road. */
uint GetMaximumRoadCatchmentRadius()
{
	uint radius = MAX_CATCHMENT + 3;
	for (uint i = 0; i < NUM_AIRPORTS; ++i) radius = std::max<uint>(radius, AirportSpec::GetWithoutOverride(i)->catchment + 3);
	return radius;
}

static TileIndex RoadNeighbour(TileIndex tile, DiagDirection dir)
{
	TileIndexDiffC offset = TileIndexDiffCByDiagDir(dir);
	return TileAddWrap(tile, offset.x, offset.y);
}

/** Bounded multi-source road search; the best remaining budget wins at junctions. */
BitmapTileArea BuildRoadCatchment(std::span<const RoadCatchmentSource> sources, const TileArea *future_footprint)
{
	TileArea bounds;
	for (const auto &source : sources) bounds.Add(TileArea(source.area).Expand(std::max(source.local_radius, source.road_distance + 1)));
	BitmapTileArea covered;
	if (bounds.IsEmpty()) return covered;
	covered.Initialize(bounds);
	std::map<TileIndex, uint> remaining;
	std::priority_queue<std::pair<uint, TileIndex>> queue;
	auto road_bits = [&](TileIndex tile) {
		if (tile == INVALID_TILE || (future_footprint != nullptr && future_footprint->Contains(tile))) return RoadBits{};
		return GetAnyRoadBits(tile, RoadTramType::Road, true);
	};
	auto visit = [&](TileIndex tile, uint budget) {
		if (road_bits(tile).None()) return;
		auto [it, inserted] = remaining.try_emplace(tile, budget);
		if (inserted || budget > it->second) {
			it->second = budget;
			queue.emplace(budget, tile);
		}
	};
	for (const auto &source : sources) {
		for (TileIndex tile : TileArea(source.area).Expand(source.local_radius)) covered.Add(tile);
		if (source.road_distance == 0) continue;
		for (TileIndex tile : source.area) {
			for (DiagDirection dir : EnumRange(DiagDirection::End)) {
				if (source.dock_direction.has_value() && dir != ReverseDiagDir(*source.dock_direction)) continue;
				TileIndex road = RoadNeighbour(tile, dir);
				if (source.area.Contains(road)) continue;
				if (source.dock_direction.has_value() && !road_bits(road).Any(DiagDirToRoadBits(ReverseDiagDir(dir)))) continue;
				visit(road, source.road_distance - 1);
			}
		}
	}
	while (!queue.empty()) {
		auto [budget, tile] = queue.top();
		queue.pop();
		if (remaining.at(tile) != budget) continue;
		covered.Add(tile);
		RoadBits bits = road_bits(tile);
		for (DiagDirection dir : EnumRange(DiagDirection::End)) {
			TileIndex neighbour = RoadNeighbour(tile, dir);
			if (neighbour != INVALID_TILE) covered.Add(neighbour);
			if (budget == 0 || !bits.Any(DiagDirToRoadBits(dir))) continue;
			uint cost = 1;
			if (IsTileType(tile, TileType::TunnelBridge) && dir == GetTunnelBridgeDirection(tile)) {
				neighbour = GetOtherTunnelBridgeEnd(tile);
				cost = DistanceManhattan(tile, neighbour);
			}
			if (cost <= budget && road_bits(neighbour).Any(DiagDirToRoadBits(ReverseDiagDir(dir)))) visit(neighbour, budget - cost);
		}
	}
	return covered;
}

/** Queue affected stations while road/rail setters may still be constructing a tile. */
void InvalidateStationRoadAccess(TileIndex tile)
{
	if (!_settings_game.station.road_access_catchment) return;
	++_road_access_revision;
	for (const Station *st : Station::Iterate()) {
		if (!st->spread.IsEmpty() && TileArea(st->spread).Expand(st->GetCatchmentRadius()).Contains(tile)) _dirty_road_catchments.insert(st->index);
	}
}

/** Flush after a complete command (or before the next simulation tick). */
void UpdateStationRoadAccess()
{
	if (_dirty_road_catchments.empty()) return;
	auto dirty = std::move(_dirty_road_catchments);
	_dirty_road_catchments.clear();
	if (!_settings_game.station.road_access_catchment) return;
	for (StationID id : dirty) {
		Station *st = Station::GetIfValid(id);
		if (st == nullptr) continue;
		st->RecomputeCatchment();
		UpdateStationAcceptance(st, false);
	}
	MarkWholeScreenDirty();
}

void ResetStationRoadAccess()
{
	_dirty_road_catchments.clear();
	++_road_access_revision;
}

uint64_t GetStationRoadAccessRevision() { return _road_access_revision; }
