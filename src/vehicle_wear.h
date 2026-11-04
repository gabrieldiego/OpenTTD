/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */

/** @file vehicle_wear.h Usage-based vehicle condition, costs and maintenance. */
#ifndef VEHICLE_WEAR_H
#define VEHICLE_WEAR_H

#include "vehicle_type.h"
#include "economy_type.h"
#include "timer/timer_game_calendar.h"

uint64_t GetVehicleLifetimeTicks(const Vehicle *v);
uint64_t GetVehicleOperatingHours(const Vehicle *v);
uint64_t GetVehicleServiceIntervalTicks(const Vehicle *v);
uint64_t GetVehicleServiceHours(const Vehicle *v);
TimerGameCalendar::Date GetVehicleUsageAge(const Vehicle *v);
uint GetVehicleRunningCostPercent(const Vehicle *v);
uint GetVehicleMaintenanceCostPercent(const Vehicle *v);
uint32_t GetVehicleWearFailureChance(const Vehicle *v, uint64_t exposure);
Money ApplyVehicleRunningCostWear(const Vehicle *v, Money cost);
Money GetVehicleMaintenanceCost(const Vehicle *v);
void RecordVehicleUsage(Vehicle *front);
void UpdateVehicleWear(Vehicle *v);
void ServiceVehicleWear(Vehicle *v);
void ChargeVehicleMaintenance(Vehicle *front);
bool CheckVehicleWearFailure(Vehicle *v);
void InitializeLegacyVehicleWear(Vehicle *v);
void SynchronizeVehicleWear(Vehicle *v);

#endif
