/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_catchment.h Road-access station catchment calculation. */

#ifndef STATION_CATCHMENT_H
#define STATION_CATCHMENT_H

#include "tilearea_bitmap.h"
#include "station_type.h"
#include "rail_type.h"
#include "direction_type.h"

/** A facility's local catchment and road access budget. */
struct RoadCatchmentSource {
	TileArea area;
	uint local_radius;
	uint road_distance;
	std::optional<DiagDirection> dock_direction; ///< Seaward direction; only the opposite land edge admits roads.
};

RoadCatchmentSource MakeRoadCatchmentSource(const TileArea &area, StationType type, uint airport_catchment = CA_UNMODIFIED, RailType railtype = RAILTYPE_RAIL, std::optional<DiagDirection> dock_direction = std::nullopt);
BitmapTileArea BuildRoadCatchment(std::span<const RoadCatchmentSource> sources, const TileArea *future_footprint = nullptr);
uint GetMaximumRoadCatchmentRadius();
void InvalidateStationRoadAccess(TileIndex tile);
void UpdateStationRoadAccess();
void ResetStationRoadAccess();
uint64_t GetStationRoadAccessRevision();

#endif /* STATION_CATCHMENT_H */
