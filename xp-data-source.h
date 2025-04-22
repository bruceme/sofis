/*
 * SPDX-FileCopyrightText: 2021 Samuel Cuella <samuel.cuella@gmail.com>
 *
 * This file is part of SoFIS - an open source EFIS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef XP_DATA_SOURCE_H
#define XP_DATA_SOURCE_H

#include "data-source.h"

// Structs for parsed data
typedef struct {
    float pitch, roll, heading,sideslip;  // Attitude (degrees)
} Attitude;

typedef struct {
    float latitude, longitude, altitude;  // Position (geographical data)
} Position;

typedef struct {
    float indicated_airspeed, true_airspeed, vertical, groundspeed;  // Airspeed (knots)
} Airspeed;

typedef struct {
    float rpm;  // Engine data
    float oil_press;  // Engine data
    float oil_temp;  // Engine data
    float egt;  // Engine data
    float cht;  // Engine data
    float man_press;  // Engine data
    float fuelflow;
    float fuel_press;  // Engine data
    float fuelQuantity[4];
    float battery_volts;  // Engine data
} Engine;


typedef struct{
    DataSource super;

    // FlightgearConnector *fglink;
    int port;
}XPDataSource;

XPDataSource *xp_data_source_new2(int port);
XPDataSource *xp_data_source_init(XPDataSource *self, int port);

#endif /* XP_DATA_SOURCE_H */
