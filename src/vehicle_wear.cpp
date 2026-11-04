/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */

/** @file vehicle_wear.cpp Usage-based condition and maintenance economics. */
#include "stdafx.h"
#include "vehicle_wear.h"
#include "vehicle_base.h"
#include "engine_base.h"
#include "train.h"
#include "texteff.hpp"
#include "company_func.h"
#include "economy_func.h"
#include "news_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "core/random_func.hpp"
#include "table/strings.h"
#include "safeguards.h"

static constexpr uint64_t USAGE_YEAR = CalendarTime::DAYS_IN_YEAR * Ticks::DAY_TICKS;

uint64_t GetVehicleLifetimeTicks(const Vehicle *v)
{
	int days = v->max_age.base();
	if (days <= 0) days = std::max(v->GetEngine()->GetLifeLengthInDays().base(), 30 * CalendarTime::DAYS_IN_YEAR);
	return uint64_t(days) * Ticks::DAY_TICKS;
}

uint64_t GetVehicleOperatingHours(const Vehicle *v) { return v->usage_ticks / Ticks::DAY_TICKS * 24 + v->usage_ticks % Ticks::DAY_TICKS * 24 / Ticks::DAY_TICKS; }
uint64_t GetVehicleServiceHours(const Vehicle *v) { return v->service_usage_ticks / Ticks::DAY_TICKS * 24 + v->service_usage_ticks % Ticks::DAY_TICKS * 24 / Ticks::DAY_TICKS; }
TimerGameCalendar::Date GetVehicleUsageAge(const Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return v->age;
	return TimerGameCalendar::Date{static_cast<int>(std::min<uint64_t>(CalendarTime::MAX_DATE.base(), v->usage_ticks / Ticks::DAY_TICKS))};
}

static uint UsagePercent(uint64_t usage, uint64_t budget)
{
	budget = std::max<uint64_t>(1, budget);
	return usage >= budget * 10 ? 1000 : static_cast<uint>(usage * 100 / budget);
}

uint64_t GetVehicleServiceIntervalTicks(const Vehicle *v)
{
	/* Preserve existing interval storage, but measure moving time rather than elapsed dates. */
	uint days = v->ServiceIntervalIsPercent() || v->GetServiceInterval() == 0 ? 180 : v->GetServiceInterval() * (TimerGameEconomy::UsingWallclockUnits() ? EconomyTime::DAYS_IN_ECONOMY_MONTH : 1);
	return uint64_t(days) * Ticks::DAY_TICKS;
}

static uint ServicePercent(const Vehicle *v)
{
	return UsagePercent(v->service_usage_ticks, GetVehicleServiceIntervalTicks(v));
}

uint GetVehicleRunningCostPercent(const Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return 100;
	uint age = UsagePercent(v->usage_ticks, GetVehicleLifetimeTicks(v));
	uint excess = age > 100 ? age - 100 : 0;
	uint neglected = ServicePercent(v);
	return std::min(500u, 100 + age / 5 + (age >= 100 ? 35 : 0) + 3 * excess * excess / 100 + std::min(50u, neglected > 100 ? (neglected - 100) / 4 : 0));
}

Money ApplyVehicleRunningCostWear(const Vehicle *v, Money cost)
{
	if (!v->GetEngine()->UsesUsageAging()) return cost;
	uint percent = GetVehicleRunningCostPercent(v);
	return cost / 100 * percent + cost % 100 * percent / 100;
}

uint GetVehicleMaintenanceCostPercent(const Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return 100;
	uint age = UsagePercent(v->usage_ticks, GetVehicleLifetimeTicks(v));
	uint excess = age > 100 ? age - 100 : 0;
	return std::min(1000u, 100 + age / 2 + (age >= 100 ? 75 : 0) + 5 * excess + excess * excess / 50 + ServicePercent(v) / 4);
}

Money GetVehicleMaintenanceCost(const Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging() || v->service_usage_ticks == 0) return 0;
	Money price = v->GetEngine()->GetCost();
	/* A multiheaded model's purchase price includes both ends. */
	if (v->type == VehicleType::Train && Train::From(v)->IsMultiheaded()) price /= 2;
	return std::max<Money>(1, price / 200) * GetVehicleMaintenanceCostPercent(v) / 100;
}

void RecordVehicleUsage(Vehicle *front)
{
	if (_game_mode == GameMode::Menu || !front->IsPrimaryVehicle() || front->cur_speed == 0 || front->vehstatus.Any({VehState::Stopped, VehState::Crashed})) return;
	for (Vehicle *v = front; v != nullptr && v->HasEngineType(); v = front->type == VehicleType::Train ? v->Next() : nullptr) {
		if (!v->GetEngine()->UsesUsageAging()) continue;
		if (v->usage_ticks != UINT64_MAX) v->usage_ticks++;
		if (v->service_usage_ticks != UINT64_MAX) v->service_usage_ticks++;
	}
}

void UpdateVehicleWear(Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return;
	if (v->base_reliability == 0) v->base_reliability = v->GetEngine()->GetBaseReliability();
	uint service = ServicePercent(v);
	uint loss = std::min(85u, service / 10 + (service > 100 ? (service - 100) / 4 : 0));
	uint age = UsagePercent(v->usage_ticks, GetVehicleLifetimeTicks(v));
	loss += std::min(35u, age / 10 + (age > 100 ? (age - 100) / 5 : 0));
	v->reliability = v->base_reliability * (100 - std::min(95u, loss)) / 100;
	SetWindowDirty(WindowClass::VehicleDetails, v->index);

	uint level = v->usage_ticks >= GetVehicleLifetimeTicks(v) ? 2 + std::min<uint64_t>(3, (v->usage_ticks - GetVehicleLifetimeTicks(v)) / USAGE_YEAR) :
		v->usage_ticks + USAGE_YEAR >= GetVehicleLifetimeTicks(v) ? 1 : 0;
	if (level > v->wear_warning_level) {
		v->wear_warning_level = level;
		if (_game_mode == GameMode::Normal && _settings_client.gui.old_vehicle_warn && v->IsPrimaryVehicle() && v->owner == _local_company && !v->vehstatus.Test(VehState::Crashed)) {
			AddVehicleAdviceNewsItem(AdviceType::VehicleOld, GetEncodedString(level == 1 ? STR_NEWS_VEHICLE_WEAR_NEAR_LIFE : STR_NEWS_VEHICLE_WEAR_EXPIRED, v->index), v->index);
		}
	}
}

void ServiceVehicleWear(Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return;
	v->service_usage_ticks = 0;
	v->breakdown_usage_ticks = v->usage_ticks;
	UpdateVehicleWear(v);
}

void ChargeVehicleMaintenance(Vehicle *front)
{
	Money cost = 0;
	for (Vehicle *v = front; v != nullptr && v->HasEngineType(); v = front->type == VehicleType::Train ? Train::From(v)->GetNextVehicle() : nullptr) cost += GetVehicleMaintenanceCost(v);
	if (cost == 0 || _game_mode == GameMode::Menu) return;
	static const VehicleTypeIndexArray<ExpensesType> expenses = {ExpensesType::TrainRun, ExpensesType::RoadVehRun, ExpensesType::ShipRun, ExpensesType::AircraftRun};
	SubtractMoneyFromCompany(front->owner, CommandCost(expenses[front->type], cost));
	front->profit_this_year -= cost << 8;
	if (front->owner == _local_company) ShowCostOrIncomeAnimation(front->x_pos, front->y_pos, front->z_pos, cost);
}

uint32_t GetVehicleWearFailureChance(const Vehicle *v, uint64_t exposure)
{
	uint quality = v->base_reliability == 0 ? 90 : (uint(v->base_reliability) * 100 + UINT16_MAX / 2) / UINT16_MAX;
	uint condition = v->base_reliability == 0 ? 100 : std::min(100u, uint(v->reliability) * 100 / v->base_reliability);
	uint age = UsagePercent(v->usage_ticks, GetVehicleLifetimeTicks(v));
	uint64_t multiplier = uint64_t(100 + 4 * (100 - quality)) * (100 + 3 * (100 - condition)) * (100 + (age > 100 ? 3 * (age - 100) : 0)) / 100;
	if (_settings_game.difficulty.vehicle_breakdowns == VehicleBreakdowns::Reduced) multiplier /= 4;
	constexpr uint64_t denominator = 3650ULL * Ticks::DAY_TICKS * 10000;
	if (exposure >= denominator / std::max<uint64_t>(1, multiplier)) return UINT32_MAX;
	return static_cast<uint32_t>(exposure * multiplier * UINT32_MAX / denominator);
}

bool CheckVehicleWearFailure(Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return false;
	UpdateVehicleWear(v);
	uint64_t exposure = v->usage_ticks - std::min(v->usage_ticks, v->breakdown_usage_ticks);
	v->breakdown_usage_ticks = v->usage_ticks;
	if (_settings_game.difficulty.vehicle_breakdowns == VehicleBreakdowns::None || v->vehstatus.Any({VehState::Stopped, VehState::Crashed})) return false;
	return exposure > 0 && Random() < GetVehicleWearFailureChance(v, exposure);
}

void InitializeLegacyVehicleWear(Vehicle *v)
{
	if (!v->GetEngine()->UsesUsageAging()) return;
	/* Historical running time was not recorded: retain calendar age as the conservative estimate. */
	v->usage_ticks = uint64_t(std::max(0, v->age.base())) * Ticks::DAY_TICKS;
	uint64_t since_service = uint64_t(std::max(0, (TimerGameEconomy::date - v->date_of_last_service).base())) * Ticks::DAY_TICKS;
	v->service_usage_ticks = std::min(v->usage_ticks, since_service);
	v->breakdown_usage_ticks = v->usage_ticks;
	v->max_age = v->GetEngine()->GetLifeLengthInDays();
	v->base_reliability = v->GetEngine()->GetBaseReliability();
	v->wear_warning_level = 0;
	/* A legacy aircraft fault is repaired on the ground under the new model. */
	if (v->type == VehicleType::Aircraft) {
		v->vehstatus.Reset(VehState::AircraftBroken);
		v->breakdown_ctr = 0;
	}
}

/** Reconcile a saved vehicle with its model's current NewGRF aging capability. */
void SynchronizeVehicleWear(Vehicle *v)
{
	const Engine *e = v->GetEngine();
	if (e->UsesUsageAging()) {
		/* Also covers version-371 saves whose default models did not yet record usage. */
		if (v->base_reliability == 0) {
			if (v->usage_ticks == 0 && v->service_usage_ticks == 0) {
				InitializeLegacyVehicleWear(v);
			} else {
				/* Preserve any measured history even if its quality had not been initialized. */
				v->base_reliability = e->GetBaseReliability();
				v->max_age = e->GetLifeLengthInDays();
			}
		}
		return;
	}
	if (v->base_reliability == 0) return; // This model has always used classic aging.

	/* Restore classic state when a model opts out, including ships in older NextGRF saves. */
	v->usage_ticks = v->service_usage_ticks = v->breakdown_usage_ticks = 0;
	v->base_reliability = 0;
	v->wear_warning_level = 0;
	v->max_age = e->GetLifeLengthInDays();
	v->reliability = e->reliability;
	v->reliability_spd_dec = e->reliability_spd_dec;
	if (v->IsPrimaryVehicle() || (v->type == VehicleType::Train && Train::From(v)->IsEngine())) {
		for (int i = 0; i <= 4; ++i) {
			if (v->age - v->max_age >= TimerGameCalendar::DateAtStartOfYear(TimerGameCalendar::Year{i})) v->reliability_spd_dec <<= 1;
		}
	}
}
