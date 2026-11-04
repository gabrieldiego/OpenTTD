/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file vehicle_wear.cpp Tests for usage, maintenance economics and airport faults. */
#include "../stdafx.h"
#include "../3rdparty/catch2/catch.hpp"
#include "../aircraft.h"
#include "../train.h"
#include "../roadveh.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../vehicle_wear.h"
#include "../vehicle_func.h"
#include "../core/backup_type.hpp"
#include "../core/random_func.hpp"
#include "../company_func.h"
#include "../company_base.h"
#include "../economy_func.h"
#include "../safeguards.h"

class WearFixture {
	AutoRestoreBackup<GameMode> mode{_game_mode, GameMode::Normal};
	AutoRestoreBackup<bool> warnings{_settings_client.gui.old_vehicle_warn, false};
	AutoRestoreBackup<EconomySettings> economy{_settings_game.economy};
	AutoRestoreBackup<VehicleBreakdowns> breakdowns{_settings_game.difficulty.vehicle_breakdowns, VehicleBreakdowns::Normal};
public:
	Engine *engine;
	Aircraft *plane;
	WearFixture()
	{
		_settings_game.economy.timekeeping_units = TimekeepingUnits::Calendar;
		REQUIRE(Engine::CanAllocateItem());
		engine = Engine::Create(VehicleType::Aircraft, 0);
		engine->reliability_max = UINT16_MAX * 90 / 100;
		engine->manufacturer_reliability = engine->reliability_max;
		plane = MakePlane();
	}
	~WearFixture() { _vehicle_pool.CleanPool(); _engine_pool.CleanPool(); _company_pool.CleanPool(); }
	Aircraft *MakePlane()
	{
		REQUIRE(Aircraft::CanAllocateItem());
		Aircraft *v = Aircraft::Create();
		v->subtype = AIR_AIRCRAFT;
		v->engine_type = engine->index;
		v->owner = CompanyID{1};
		v->max_age = TimerGameCalendar::Date{3650};
		v->SetServiceInterval(100);
		return v;
	}
};

TEST_CASE_METHOD(WearFixture, "Usage ages moving vehicles and excludes parked, stopped and crashed vehicles", "[wear]")
{
	RecordVehicleUsage(plane);
	CHECK(plane->usage_ticks == 0);
	plane->cur_speed = 100;
	RecordVehicleUsage(plane);
	CHECK(plane->usage_ticks == 1);
	CHECK(plane->service_usage_ticks == 1);
	plane->vehstatus.Set(VehState::Stopped);
	RecordVehicleUsage(plane);
	CHECK(plane->usage_ticks == 1);
	plane->vehstatus.Reset(VehState::Stopped);
	plane->vehstatus.Set(VehState::Crashed);
	RecordVehicleUsage(plane);
	CHECK(plane->usage_ticks == 1);
}

TEST_CASE_METHOD(WearFixture, "Maintenance restores service wear but preserves lifetime age and built quality", "[wear]")
{
	UpdateVehicleWear(plane);
	uint16_t baseline = plane->base_reliability;
	plane->usage_ticks = 2 * GetVehicleLifetimeTicks(plane);
	plane->service_usage_ticks = 1000 * Ticks::DAY_TICKS;
	UpdateVehicleWear(plane);
	uint running = GetVehicleRunningCostPercent(plane);
	uint repair = GetVehicleMaintenanceCostPercent(plane);
	uint16_t before = plane->reliability;
	engine->manufacturer_reliability /= 2; // A changed definition must not alter the vehicle's built quality.
	ServiceVehicleWear(plane);
	CHECK(plane->usage_ticks == 2 * GetVehicleLifetimeTicks(plane));
	CHECK(plane->service_usage_ticks == 0);
	CHECK(plane->base_reliability == baseline);
	CHECK(plane->reliability > before);
	CHECK(plane->reliability < baseline);
	CHECK(GetVehicleRunningCostPercent(plane) < running);
	CHECK(GetVehicleRunningCostPercent(plane) > 100);
	CHECK(GetVehicleMaintenanceCostPercent(plane) < repair);
	CHECK(GetVehicleMaintenanceCost(plane) == 0); // Repeated depot checks cannot charge twice.
}

TEST_CASE_METHOD(WearFixture, "Expiry has a substantial cost increase at the rated usage lifetime", "[wear]")
{
	plane->usage_ticks = GetVehicleLifetimeTicks(plane) - 1;
	uint running = GetVehicleRunningCostPercent(plane);
	uint repair = GetVehicleMaintenanceCostPercent(plane);
	plane->usage_ticks++;
	CHECK(GetVehicleRunningCostPercent(plane) >= running + 35);
	CHECK(GetVehicleMaintenanceCostPercent(plane) >= repair + 75);
	UpdateVehicleWear(plane);
	CHECK(plane->wear_warning_level == 2);
	plane->usage_ticks = 10 * GetVehicleLifetimeTicks(plane);
	CHECK(GetVehicleRunningCostPercent(plane) == 500);
	CHECK(GetVehicleMaintenanceCostPercent(plane) == 1000);
}

TEST_CASE_METHOD(WearFixture, "Calendar age does not wear an unused vehicle", "[wear]")
{
	plane->age = TimerGameCalendar::Date{36500};
	UpdateVehicleWear(plane);
	CHECK(plane->reliability == plane->base_reliability);
	CHECK(GetVehicleUsageAge(plane) == TimerGameCalendar::Date{0});
	CHECK(GetVehicleRunningCostPercent(plane) == 100);
	CHECK(plane->wear_warning_level == 0);
}

TEST_CASE_METHOD(WearFixture, "Every vehicle has an independent operating-hours schedule", "[wear]")
{
	Aircraft *other = MakePlane();
	plane->SetServiceInterval(30);
	other->SetServiceInterval(60);
	plane->usage_ticks = other->usage_ticks = 40 * Ticks::DAY_TICKS;
	plane->service_usage_ticks = other->service_usage_ticks = plane->usage_ticks;
	CHECK(GetVehicleServiceIntervalTicks(plane) == 30 * Ticks::DAY_TICKS);
	CHECK(GetVehicleServiceIntervalTicks(other) == 60 * Ticks::DAY_TICKS);
	CHECK(GetVehicleOperatingHours(plane) == 960);
	UpdateVehicleWear(plane); UpdateVehicleWear(other);
	CHECK(plane->reliability < other->reliability);
	CHECK(GetVehicleRunningCostPercent(plane) > GetVehicleRunningCostPercent(other));
}

TEST_CASE_METHOD(WearFixture, "Disabled automatic servicing retains a reasonable manual maintenance budget", "[wear]")
{
	plane->SetServiceInterval(0);
	CHECK(GetVehicleServiceIntervalTicks(plane) == 180 * Ticks::DAY_TICKS);
	plane->usage_ticks = plane->service_usage_ticks = Ticks::DAY_TICKS;
	UpdateVehicleWear(plane);
	CHECK(plane->reliability == plane->base_reliability);
}

TEST_CASE_METHOD(WearFixture, "Failure risk follows exposure, condition and quality with bounded arithmetic", "[wear]")
{
	UpdateVehicleWear(plane);
	uint64_t day = Ticks::DAY_TICKS;
	CHECK(GetVehicleWearFailureChance(plane, 0) == 0);
	uint32_t normal = GetVehicleWearFailureChance(plane, day);
	CHECK(normal > 0);
	CHECK(GetVehicleWearFailureChance(plane, 2 * day) > normal);
	_settings_game.difficulty.vehicle_breakdowns = VehicleBreakdowns::Reduced;
	CHECK(GetVehicleWearFailureChance(plane, day) < normal);
	_settings_game.difficulty.vehicle_breakdowns = VehicleBreakdowns::Normal;
	plane->usage_ticks = plane->service_usage_ticks = GetVehicleLifetimeTicks(plane) * 2;
	UpdateVehicleWear(plane);
	CHECK(GetVehicleWearFailureChance(plane, day) > normal);
	CHECK(GetVehicleWearFailureChance(plane, UINT64_MAX) == UINT32_MAX);
}

TEST_CASE_METHOD(WearFixture, "Aircraft faults occur at terminals and force unloading before hangar service", "[wear]")
{
	plane->usage_ticks = plane->service_usage_ticks = GetVehicleLifetimeTicks(plane) * 10;
	plane->cur_speed = 2000;
	CheckVehicleBreakdown(plane);
	CHECK(plane->breakdown_ctr == 0);
	CHECK_FALSE(plane->vehstatus.Test(VehState::AircraftBroken));
	plane->targetairport = StationID{4};
	plane->cur_real_order_index = 2;
	plane->current_order.MakeGoToStation(plane->targetairport);
	SetRandomSeed(42);
	BeginAircraftMaintenance(plane);
	CHECK(plane->maintenance_state == 1);
	CHECK(plane->maintenance_station == StationID{4});
	CHECK(plane->maintenance_order_index == 2);
	CHECK(plane->current_order.GetLoadType() == OrderLoadType::NoLoad);
	CHECK(plane->current_order.GetUnloadType() == OrderUnloadType::Unload);
	CHECK_FALSE(plane->vehstatus.Test(VehState::AircraftBroken));
	CHECK_FALSE(CheckVehicleWearFailure(plane)); // No second failure without more usage.
}

TEST_CASE_METHOD(WearFixture, "Legacy saves retain an explicit age estimate and clear airborne faults", "[wear]")
{
	plane->age = TimerGameCalendar::Date{7300};
	plane->vehstatus.Set(VehState::AircraftBroken);
	plane->breakdown_ctr = 1;
	InitializeLegacyVehicleWear(plane);
	CHECK(plane->usage_ticks == 7300 * Ticks::DAY_TICKS);
	CHECK(plane->breakdown_usage_ticks == plane->usage_ticks);
	CHECK(plane->service_usage_ticks <= plane->usage_ticks);
	CHECK(plane->breakdown_ctr == 0);
	CHECK_FALSE(plane->vehstatus.Test(VehState::AircraftBroken));
}

TEST_CASE_METHOD(WearFixture, "Unopted vehicles keep calendar age, free service and unchanged cost arithmetic", "[wear][compatibility]")
{
	engine->manufacturer_reliability = 0;
	plane->age = TimerGameCalendar::Date{36500};
	plane->usage_ticks = plane->service_usage_ticks = 1000000;
	plane->reliability = 12345;
	plane->cur_speed = 100;
	RecordVehicleUsage(plane);
	UpdateVehicleWear(plane);
	CHECK(plane->usage_ticks == 1000000);
	CHECK(plane->reliability == 12345);
	CHECK(GetVehicleUsageAge(plane) == plane->age);
	CHECK(GetVehicleRunningCostPercent(plane) == 100);
	CHECK(GetVehicleMaintenanceCost(plane) == 0);
	CHECK(ApplyVehicleRunningCostWear(plane, Money{INT64_MAX}) == Money{INT64_MAX});
	BeginAircraftMaintenance(plane);
	CHECK(plane->maintenance_state == 0);
	plane->vehstatus.Set(VehState::AircraftBroken);
	InitializeLegacyVehicleWear(plane);
	CHECK(plane->vehstatus.Test(VehState::AircraftBroken));
}

TEST_CASE_METHOD(WearFixture, "Classic expiration still doubles reliability decay; opted-in expiration uses wear", "[wear][compatibility]")
{
	engine->manufacturer_reliability = 0;
	plane->age = plane->max_age - TimerGameCalendar::Date{1};
	plane->reliability_spd_dec = 4;
	AgeVehicle(plane);
	CHECK(plane->reliability_spd_dec == 8);
	engine->manufacturer_reliability = UINT16_MAX * 90 / 100;
	plane->age = plane->max_age - TimerGameCalendar::Date{1};
	plane->reliability_spd_dec = 4;
	AgeVehicle(plane);
	CHECK(plane->reliability_spd_dec == 4);
}

TEST_CASE_METHOD(WearFixture, "NewGRF opt-in preserves classic lifetime and reliability for other models", "[wear][compatibility]")
{
	engine->info.lifelength = TimerGameCalendar::Year{20};
	AutoRestoreBackup<uint8_t> extension(_settings_game.vehicle.extend_vehicle_life, 0);
	CHECK(engine->GetLifeLengthInDays().base() == 20 * 365);
	engine->manufacturer_reliability = 0;
	engine->reliability = 12345;
	CHECK_FALSE(engine->UsesUsageAging());
	CHECK(engine->GetLifeLengthInDays().base() == 20 * 366);
	CHECK(engine->GetBaseReliability() == 12345);
}

TEST_CASE_METHOD(WearFixture, "Newly opted-in default vehicles initialize usage once in existing saves", "[wear][compatibility]")
{
	plane->age = TimerGameCalendar::Date{91};
	SynchronizeVehicleWear(plane);
	CHECK(plane->usage_ticks == 91 * Ticks::DAY_TICKS);
	CHECK(plane->base_reliability == engine->GetBaseReliability());
	CHECK(plane->max_age == engine->GetLifeLengthInDays());
	plane->age += TimerGameCalendar::Date{30};
	SynchronizeVehicleWear(plane);
	CHECK(plane->usage_ticks == 91 * Ticks::DAY_TICKS);

	Aircraft *unused = MakePlane();
	SynchronizeVehicleWear(unused);
	unused->age += TimerGameCalendar::Date{30};
	SynchronizeVehicleWear(unused);
	CHECK(unused->usage_ticks == 0); // Reloading cannot age an already initialized parked vehicle.
}

TEST_CASE_METHOD(WearFixture, "Initializing manufacturer quality preserves measured operating time", "[wear][compatibility]")
{
	plane->age = TimerGameCalendar::Date{91};
	plane->usage_ticks = 1234;
	plane->service_usage_ticks = 567;
	SynchronizeVehicleWear(plane);
	CHECK(plane->usage_ticks == 1234);
	CHECK(plane->service_usage_ticks == 567);
	CHECK(plane->base_reliability == engine->GetBaseReliability());
}

TEST_CASE_METHOD(WearFixture, "Opting out restores classic age and condition without touching other models", "[wear][compatibility]")
{
	UpdateVehicleWear(plane);
	plane->usage_ticks = plane->service_usage_ticks = 100000;
	engine->manufacturer_reliability = 0;
	engine->reliability = 45678;
	engine->reliability_spd_dec = 80;
	plane->age = engine->GetLifeLengthInDays();
	SynchronizeVehicleWear(plane);
	CHECK(plane->usage_ticks == 0);
	CHECK(plane->base_reliability == 0);
	CHECK(plane->max_age == engine->GetLifeLengthInDays());
	CHECK(plane->reliability == 45678);
	CHECK(plane->reliability_spd_dec == 160); // Classic decay doubles at expiry.
	CHECK(GetVehicleUsageAge(plane) == plane->age);
	plane->reliability = 12345;
	SynchronizeVehicleWear(plane);
	CHECK(plane->reliability == 12345); // Later loads cannot repair a classic vehicle.
}

TEST_CASE_METHOD(WearFixture, "A maintenance fee affects company cash, expenses and vehicle profit exactly once", "[wear][economy]")
{
	AutoRestoreBackup<CompanyID> local(_local_company, COMPANY_SPECTATOR);
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::CreateAtIndex(plane->owner, StringID{}, false);
	company->money = Money{1000000};
	REQUIRE(Engine::CanAllocateItem(2));
	Engine *rail = Engine::Create(VehicleType::Train, 0);
	Engine *road = Engine::Create(VehicleType::Road, 0);
	rail->manufacturer_reliability = road->manufacturer_reliability = UINT16_MAX * 90 / 100;
	REQUIRE(Train::CanAllocateItem());
	Train *train = Train::Create();
	train->engine_type = rail->index;
	train->SetEngine();
	train->SetFrontEngine();
	REQUIRE(RoadVehicle::CanAllocateItem());
	RoadVehicle *bus = RoadVehicle::Create();
	bus->engine_type = road->index;
	for (auto [v, expense] : std::array<std::pair<Vehicle *, ExpensesType>, 3>{{
		{plane, ExpensesType::AircraftRun}, {train, ExpensesType::TrainRun}, {bus, ExpensesType::RoadVehRun}
	}}) {
		CAPTURE(v->type);
		company->money = Money{1000000};
		v->owner = company->index;
		v->max_age = v->GetEngine()->GetLifeLengthInDays();
		v->profit_this_year = Money{10000} << 8;
		v->usage_ticks = v->service_usage_ticks = 180 * Ticks::DAY_TICKS;
		Money fee = GetVehicleMaintenanceCost(v);
		REQUIRE(fee > 0);
		ChargeVehicleMaintenance(v);
		CHECK(company->money == Money{1000000} - fee);
		CHECK(company->yearly_expenses[0][expense] == fee);
		CHECK(v->profit_this_year == ((Money{10000} - fee) << 8));
		ServiceVehicleWear(v);
		ChargeVehicleMaintenance(v);
		CHECK(company->money == Money{1000000} - fee);
		CHECK(company->yearly_expenses[0][expense] == fee);
		CHECK(v->profit_this_year == ((Money{10000} - fee) << 8));
	}
}

TEST_CASE_METHOD(WearFixture, "Train maintenance counts a two-ended model once and skips articulated sections", "[wear][economy]")
{
	AutoRestoreBackup<CompanyID> local(_local_company, COMPANY_SPECTATOR);
	AutoRestoreBackup<Money> price(_price[Price::BuildRail], Money{100000});
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::CreateAtIndex(plane->owner, StringID{}, false);
	company->money = Money{1000000};
	REQUIRE(Engine::CanAllocateItem());
	Engine *rail = Engine::Create(VehicleType::Train, 0);
	rail->manufacturer_reliability = UINT16_MAX * 90 / 100;
	REQUIRE(Train::CanAllocateItem(3));
	Train *front = Train::Create(), *section = Train::Create(), *rear = Train::Create();
	front->SetEngine();
	front->SetFrontEngine();
	for (Train *v : {front, section, rear}) {
		v->engine_type = rail->index;
		v->owner = company->index;
		v->max_age = rail->GetLifeLengthInDays();
		v->service_usage_ticks = 180 * Ticks::DAY_TICKS;
	}
	Money single = GetVehicleMaintenanceCost(front);
	front->SetMultiheaded();
	rear->SetMultiheaded();
	section->SetArticulatedPart();
	front->SetNext(section);
	section->SetNext(rear);
	Money total = GetVehicleMaintenanceCost(front) + GetVehicleMaintenanceCost(rear);
	CHECK(total > 0);
	CHECK(std::abs((total - single).base()) <= 2); // Integer rounding only; not twice the price.
	ChargeVehicleMaintenance(front);
	CHECK(company->money == Money{1000000} - total);
	CHECK(company->yearly_expenses[0][ExpensesType::TrainRun] == total);
	CHECK(front->profit_this_year == -(total << 8));
	CHECK(section->profit_this_year == 0);
	CHECK(rear->profit_this_year == 0);
}
