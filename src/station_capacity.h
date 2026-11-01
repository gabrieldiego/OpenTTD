/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_capacity.h Station storage pools and cargo patience. */

#ifndef STATION_CAPACITY_H
#define STATION_CAPACITY_H

#include "cargo_type.h"
#include "core/enum_type.hpp"

struct Station;
struct CargoSpec;

/** Passenger and mail storage are independent; all other cargo shares freight storage. */
enum class StationCargoPool : uint8_t { Passengers, Mail, Freight, End };
using StationCargoAmounts = EnumIndexArray<uint, StationCargoPool, StationCargoPool::End>;

StationCargoPool GetStationCargoPool(const CargoSpec &cs);
StationCargoAmounts GetStationCargoCapacities(const Station &st);
StationCargoAmounts GetStationCargoUsage(const Station &st);
uint GetStationCargoFreeSpace(const Station &st, CargoType cargo);
uint16_t GetCargoPatienceDecay(const CargoSpec &cs, uint used, uint capacity);
uint8_t GetStationOverallPatienceRating(const Station &st);
void RefreshStationCargoPatienceRatings(Station &st);
void ApplyStationDisasterPenalty(Station &st);
uint EnforceStationCargoCapacity(Station &st);
void UpdateStationCargoPatience(Station &st);

#endif /* STATION_CAPACITY_H */
