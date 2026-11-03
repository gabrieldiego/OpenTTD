/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file engine_retirement.cpp Tests for model phase-out dates shown in the vehicle catalog. */

#include "../stdafx.h"
#include "../3rdparty/catch2/catch.hpp"
#include "../core/backup_type.hpp"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../settings_type.h"

#include "../safeguards.h"

class EngineRetirementFixture {
	AutoRestoreBackup<VehicleSettings> settings{_settings_game.vehicle};

public:
	Engine engine{EngineID::Invalid(), VehicleType::Aircraft, 0};

	EngineRetirementFixture()
	{
		_settings_game.vehicle.never_expire_vehicles = false;
		this->engine.intro_date = Date(1940, 0, 1);
		this->engine.info.base_life = TimerGameCalendar::Year{30};
		this->engine.info.retire_early = 0;
		this->engine.duration_phase_1 = 12;
		this->engine.duration_phase_2 = 240;
		this->engine.duration_phase_3 = 120;
	}

	static TimerGameCalendar::Date Date(int year, int month, int day)
	{
		return TimerGameCalendar::ConvertYMDToDate(TimerGameCalendar::Year{year}, month, day);
	}
};

TEST_CASE_METHOD(EngineRetirementFixture, "Model phase-out uses reliability phases, not vehicle lifespan", "[engine-catalog]")
{
	CHECK(this->engine.GetRetirementDate() == Date(1971, 0, 1));
	this->engine.info.lifelength = TimerGameCalendar::Year{5};
	CHECK(this->engine.GetRetirementDate() == Date(1971, 0, 1));
	this->engine.info.retire_early = 2;
	CHECK(this->engine.GetRetirementDate() == Date(1959, 0, 1));
	this->engine.info.retire_early = -2;
	CHECK(this->engine.GetRetirementDate() == Date(1963, 0, 1));
	this->engine.info.retire_early = -20;
	CHECK(this->engine.GetRetirementDate() == Date(1971, 0, 1));
	this->engine.info.retire_early = 100;
	CHECK(this->engine.GetRetirementDate() == Date(1940, 0, 1));
}

TEST_CASE_METHOD(EngineRetirementFixture, "Model phase-out rounds introduction up to a month boundary", "[engine-catalog]")
{
	this->engine.intro_date = Date(1940, 0, 2);
	CHECK(this->engine.GetRetirementDate() == Date(1971, 1, 1));
	this->engine.intro_date = Date(1940, 11, 31);
	CHECK(this->engine.GetRetirementDate() == Date(1972, 0, 1));
}

TEST_CASE_METHOD(EngineRetirementFixture, "Permanent models have no phase-out date", "[engine-catalog]")
{
	_settings_game.vehicle.never_expire_vehicles = true;
	CHECK_FALSE(this->engine.GetRetirementDate().has_value());
	_settings_game.vehicle.never_expire_vehicles = false;
	this->engine.info.base_life = TimerGameCalendar::Year{0xFF};
	CHECK_FALSE(this->engine.GetRetirementDate().has_value());
}

TEST_CASE_METHOD(EngineRetirementFixture, "Model phase-out shares its parent's introduction when reliability is synchronized", "[engine-catalog]")
{
	REQUIRE(Engine::CanAllocateItem());
	std::unique_ptr<Engine> parent{Engine::Create(VehicleType::Aircraft, 0)};
	parent->intro_date = Date(1930, 0, 1);
	this->engine.info.variant_id = parent->index;
	this->engine.info.extra_flags.Set(ExtraEngineFlag::SyncReliability);
	CHECK(this->engine.GetRetirementDate() == Date(1961, 0, 1));
	this->engine.info.extra_flags.Reset(ExtraEngineFlag::SyncReliability);
	CHECK(this->engine.GetRetirementDate() == Date(1971, 0, 1));
}

TEST_CASE_METHOD(EngineRetirementFixture, "Model phase-out matches the purchase availability cutoff", "[engine-catalog]")
{
	this->engine.info.retire_early = GENERATE(-20, -2, 0, 2, 100);
	const auto retirement = this->engine.GetRetirementDate();
	REQUIRE(retirement.has_value());
	const auto ymd = TimerGameCalendar::ConvertDateToYMD(*retirement);
	const int age = (ymd.year.base() - 1940) * 12 + ymd.month;
	if (age > 0) {
		this->engine.age = age - 1;
		this->engine.company_avail.Set();
		CalcEngineReliability(&this->engine, false);
		CHECK(this->engine.company_avail.Any());
	}
	this->engine.age = age;
	this->engine.company_avail.Set();
	CalcEngineReliability(&this->engine, false);
	CHECK(this->engine.company_avail.None());
}
