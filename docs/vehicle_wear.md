# NewGRF usage-based vehicle aging

This local extension is opt-in **per engine model**. There is no game-wide switch.
Models without the capability retain their original calendar
aging, reliability, breakdown probability, service rules and costs.

After loading NewGRFs, OpenTTD queries callback 36 with property `0xFE`. Only the
response `0x5745` enables the extension; property `0xFF` must then return a baseline
reliability percentage from 1 to 100. Invalid/missing responses leave the classic
model active. These are private extension properties, not standard NewGRF properties.
NextGRF supplies them for default and added trains, road vehicles and aircraft
when its **Vehicle aging and maintenance**
parameter selects **Operating hours** (the default); **Classic** disables it.
Ships, including NextGRF ships, retain classic aging. Stock OpenTTD ignores these queries. Parameter value zero selects operating hours,
so older saves without this parameter also receive the default after upgrading.

The existing `vehicle_life` property supplies rated operating life: one year is
365 operating days or 8,760 operating hours. Usage accumulates while a vehicle is
moving, including airport taxiing. Parked, stopped and crashed vehicles add none.
Calendar age remains recorded for NewGRF callbacks and historical information.
Autorenewal, age sorting and replacement advice use operating age for opted-in models.
New vehicles retain their manufacturer's quality throughout their lives; the catalog
and purchase window show that quality instead of the classic release-age curve.

Each vehicle can select its own service interval in the details window. Existing
company defaults and interval storage are preserved: calendar-day intervals represent
operating days, and wallclock-minute intervals represent 30 operating days. The
opted-in vehicle UI displays both as operating hours. Percentage intervals measure
condition loss relative to the vehicle's built quality.
With automatic servicing disabled, manual-maintenance condition uses a reference
budget of 180 operating days; disabling automation does not imply daily servicing.

Servicing clears wear since the previous service and costs 0.5% of the model's
current purchase price, multiplied by its maintenance factor. Repeated servicing
without further usage costs nothing. The fee is charged to company running
expenses and the serviced vehicle's profit/loss (the front vehicle for a train),
with a red floating cost over the depot or hangar for the local company. Lifetime wear remains. Running costs gradually
increase with usage and overdue servicing, jump at rated life and rise further
afterward, up to 5 times base cost. Maintenance costs also jump at rated life and
rise up to 10 times base service cost. Existing base costs, inflation and NewGRF
running-cost callbacks still apply.

Failure risk uses moving-time exposure, manufacturer quality, current condition
and excess lifetime usage. The existing breakdown difficulty controls still apply.
Road vehicles, trains and ships keep their existing breakdown effects. Aircraft
check accumulated risk and automatic-service needs on arrival at a terminal;
they keep cruise speed in flight.
On a fault they unload every cargo compartment, visit a suitable reachable hangar,
wait for repairs, then return to the interrupted airport order and its load rules.
Shared order lists are not modified. If no suitable hangar is reachable, the aircraft
waits unloaded at the terminal. Emergency unloading can exceed station storage to
avoid trapping occupants aboard; station cargo-capacity and patience rules then apply.

Save version 371 persists usage, service wear, built quality and aircraft repair
progress. For earlier saves, opted-in vehicles start with calendar age as an explicit
estimate of historical usage, and actual usage is recorded thereafter. Other vehicles
are not migrated. Vehicles newly opted in by an updated NewGRF are also initialized
once in version-371 saves; already measured usage is preserved. Vehicles that opt
out return to calendar lifetime, classic reliability and the normal decay rate. Games saved with this extension require this OpenTTD build or a
later compatible build to reopen.
