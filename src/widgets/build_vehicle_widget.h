/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file build_vehicle_widget.h Types related to the build_vehicle widgets. */

#ifndef WIDGETS_BUILD_VEHICLE_WIDGET_H
#define WIDGETS_BUILD_VEHICLE_WIDGET_H

/** Widgets of the #BuildVehicleWindow class. */
enum BuildVehicleWidgets : WidgetID {
	WID_BV_CAPTION,                   ///< Caption of window.
	WID_BV_SORT_ASCENDING_DESCENDING, ///< Sort direction.
	WID_BV_SORT_DROPDOWN,             ///< Criteria of sorting dropdown.
	WID_BV_CARGO_FILTER_DROPDOWN,     ///< Cargo filter dropdown.
	WID_BV_FILTER,                    ///< Filter by name.
	WID_BV_SHOW_HIDDEN_ENGINES,       ///< Toggle whether to display the hidden vehicles.
	WID_BV_LIST,                      ///< List of vehicles.
	WID_BV_SCROLLBAR,                 ///< Scrollbar of list.
	WID_BV_PANEL,                     ///< Button panel.
	WID_BV_BUILD,                     ///< Build panel.
	WID_BV_SHOW_HIDE,                 ///< Button to hide or show the selected engine.
	WID_BV_BUILD_SEL,                 ///< Build button.
	WID_BV_RENAME,                    ///< Rename button.
	WID_BV_CONFIGURE_BADGES, ///< Button to configure badges.
	WID_BV_BADGE_FILTER, ///< Container for dropdown badge filters.
	WID_BV_CATALOG_SEL, ///< Optional catalog controls.
	WID_BV_CATALOG_TYPE, ///< Vehicle type in the catalog.
	WID_BV_CATALOG_NOTE, ///< Fixed comparison year and date explanation.
	WID_BV_EDIT_SEL, ///< Hide editing buttons in the catalog.
	WID_BV_CATALOG_CLOSE, ///< Close the catalog and return to NewGRF selection.
};

#endif /* WIDGETS_BUILD_VEHICLE_WIDGET_H */
