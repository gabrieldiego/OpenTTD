/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file aircraft_range.cpp Aircraft range overlay geometry and lifetime. */

#include "stdafx.h"
#include "aircraft_range.h"
#include "aircraft.h"
#include "station_base.h"
#include "window_func.h"
#include "gfx_func.h"
#include "widgets/vehicle_widget.h"

#include "safeguards.h"

static VehicleID _range_vehicle = VehicleID::Invalid();
static std::optional<AircraftRange> _aircraft_range;

bool AircraftRange::Contains(TileIndex tile) const
{
	return IsValidTile(this->origin) && IsValidTile(tile) &&
			(this->range == 0 || DistanceSquare(this->origin, tile) <= static_cast<uint32_t>(this->range) * this->range);
}

bool AircraftRange::ContainsAirport(const Station &st) const
{
	return !st.airport.IsEmpty() && this->Contains(st.airport.tile);
}

std::optional<AircraftRange> GetAircraftRange(const Vehicle *v)
{
	if (v == nullptr || v->type != VehicleType::Aircraft || !v->IsPrimaryVehicle() ||
			v->vehstatus.Test(VehState::Crashed) || !IsValidTile(v->tile) || !v->IsStoppedInDepot()) return std::nullopt;
	const Station *st = Station::GetByTile(v->tile);
	if (st->airport.IsEmpty()) return std::nullopt;
	return AircraftRange{st->airport.tile, Aircraft::From(v)->GetRange()};
}

const AircraftRange *GetAircraftRangeHighlight() { return _aircraft_range.has_value() ? &*_aircraft_range : nullptr; }

bool IsAircraftRangeHighlighted(VehicleID vehicle) { return vehicle != VehicleID::Invalid() && _range_vehicle == vehicle; }

/** Only the owning window can turn its overlay off. Switching aircraft invalidates both buttons. */
void SetAircraftRangeHighlight(VehicleID vehicle, bool show)
{
	if (!show && vehicle != _range_vehicle) return;
	auto range = show ? GetAircraftRange(Vehicle::GetIfValid(vehicle)) : std::nullopt;
	VehicleID selected = range.has_value() ? vehicle : VehicleID::Invalid();
	if (selected == _range_vehicle && range == _aircraft_range) return;
	if (_range_vehicle != VehicleID::Invalid()) SetWindowWidgetDirty(WindowClass::VehicleView, _range_vehicle, WID_VV_AIRCRAFT_RANGE);
	_range_vehicle = selected;
	_aircraft_range = range;
	if (_range_vehicle != VehicleID::Invalid()) SetWindowWidgetDirty(WindowClass::VehicleView, _range_vehicle, WID_VV_AIRCRAFT_RANGE);
	MarkWholeScreenDirty();
}

/** Called before drawing so departure, sale, refitting, and airport changes clear stale pixels. */
void UpdateAircraftRangeHighlight()
{
	if (_range_vehicle != VehicleID::Invalid()) SetAircraftRangeHighlight(_range_vehicle, true);
}
