/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file aircraft_range.h Aircraft range overlays, using the flight distance rule. */

#ifndef AIRCRAFT_RANGE_H
#define AIRCRAFT_RANGE_H

#include "tile_type.h"
#include "vehicle_type.h"

struct Station;

struct AircraftRange {
	TileIndex origin = INVALID_TILE; ///< Airport reference tile, as used for actual flights.
	uint16_t range = 0; ///< Maximum distance in tiles; zero means unlimited.

	bool Contains(TileIndex tile) const;
	bool ContainsAirport(const Station &st) const;
	bool operator==(const AircraftRange &) const = default;
};

std::optional<AircraftRange> GetAircraftRange(const Vehicle *v);
const AircraftRange *GetAircraftRangeHighlight();
bool IsAircraftRangeHighlighted(VehicleID vehicle);
void SetAircraftRangeHighlight(VehicleID vehicle, bool show);
void UpdateAircraftRangeHighlight();

#endif /* AIRCRAFT_RANGE_H */
