/*
 * SPDX-FileCopyrightText: 2021 Samuel Cuella <samuel.cuella@gmail.com>
 *
 * This file is part of SoFIS - an open source EFIS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <arpa/inet.h>

#include "data-source.h"
#include "xp-data-source.h"

// Lookup table for row IDs and their corresponding parsers
typedef void (*RowParser)(float values[]);
RowParser lookup_table[100] = {NULL};  // Supports row IDs up to 54

// Initialize structs
Attitude attitude = {0};
Position position = {0};
Airspeed airspeed = {0};
Engine engine = {0};

// Row parsers
void parse_attitude(float values[]) {
    attitude.pitch = values[0];
    attitude.roll = values[1];
    attitude.heading = values[3];
}
void parse_position(float values[]) {
    position.latitude = values[0];
    position.longitude = values[1];
    position.altitude = values[2];
}
void parse_airspeeds(float values[]) {
    airspeed.indicated_airspeed = values[0];
    airspeed.true_airspeed = values[2];
    airspeed.groundspeed = values[3];    
}
void parse_sideslip(float values[]){attitude.sideslip = values[7];}
void parse_rpm(float values[]) {    engine.rpm = values[0];}
void parse_fuelflow(float values[]) {    engine.fuelflow = values[0];}
void parse_egt(float values[]) {    engine.egt = values[0];}
void parse_cht(float values[]) {   engine.cht = values[0];}
void parse_oil_press(float values[]) {  engine.oil_press = values[0]; }
void parse_oil_temp(float values[]) { engine.oil_temp = values[0];  }
void parse_man_press(float values[]) { engine.man_press = values[0];  }
void parse_fuel_press(float values[]) { engine.fuel_press = values[0];  }
void parse_volts(float values[]) { engine.battery_volts = values[0];  }
void parse_vvi(float values[]) {airspeed.vertical = values[2];}
void parse_fuel_quantity(float values[])
{
    for (int i=3; i >= 0 ; i--)
        engine.fuelQuantity[i] = values[i];
}

// Initialize lookup table
void initialize_lookup_table() {
    lookup_table[3] = (RowParser)parse_airspeeds;  // Attitude
    lookup_table[4] = (RowParser)parse_vvi;
    lookup_table[17] = (RowParser)parse_attitude;  // Attitude
    lookup_table[18] = (RowParser)parse_sideslip;  
    lookup_table[20] = (RowParser)parse_position; // Position
    lookup_table[37] = (RowParser)parse_rpm;  
    lookup_table[43] = (RowParser)parse_man_press;  
    lookup_table[45] = (RowParser)parse_fuelflow;  
    lookup_table[47] = (RowParser)parse_egt;  
    lookup_table[48] = (RowParser)parse_cht;
    lookup_table[49] = (RowParser)parse_oil_press;
    lookup_table[50] = (RowParser)parse_oil_temp;
    lookup_table[51] = (RowParser)parse_fuel_press;
    lookup_table[54] = (RowParser)parse_volts;
    lookup_table[62] = (RowParser)parse_fuel_quantity;
    // Add other row IDs and their handlers as needed
}



static bool xp_data_source_frame(XPDataSource *self, uint32_t dt);
static XPDataSource *xp_data_source_dispose(XPDataSource *self);
static DataSourceOps xp_data_source_ops = {
    .frame = (DataSourceFrameFunc)xp_data_source_frame,
    .dispose = (DataSourceDisposeFunc)xp_data_source_dispose
};

XPDataSource *xp_data_source_new2(int port)
{
    XPDataSource *self;

    self = calloc(1, sizeof(XPDataSource));
    if(self){
        if(!xp_data_source_init(self, port)){
            free(self);
            return NULL;
        }
    }

    return self;
}

int sockfd;
struct sockaddr_in server_addr, client_addr;
socklen_t addr_len = sizeof(client_addr);
#define XP_SERVER_PORT 49000         // Port that X-Plane sends data to
#define BUFFER_SIZE 1024

XPDataSource *xp_data_source_init(XPDataSource *self, int port)
{
    if(!data_source_init(DATA_SOURCE(self), &xp_data_source_ops))
        return NULL;

    initialize_lookup_table();

    self->port = port;

    // Create the socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return self;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket to the port
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return self;
    }

    printf("Server is listening on port %d...\n", port);

    return self;
}

static XPDataSource *xp_data_source_dispose(XPDataSource *self)
{
    close(sockfd);
    sockfd = -1;
    return self;
}

static bool xp_data_source_frame(XPDataSource *self, uint32_t dt)
{
    char buffer[BUFFER_SIZE];

     int bytes_received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, 
                                      (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_received < 0 || bytes_received > BUFFER_SIZE) {
            //perror("Failed to receive data");
            return true;
        }

        buffer[bytes_received] = '\0';

        // Process the DATA* packet
        if (strncmp(buffer, "DATA*", 5) == 0) {
            int num_rows = (bytes_received - 5) / 36;  // Each row is 36 bytes
            for (int i = 0; i < num_rows; ++i) {
                int row_id;
                float values[8];

                memcpy(&row_id, buffer + 5 + i * 36, 4);  // Read row ID
                memcpy(values, buffer + 5 + i * 36 + 4, sizeof(values));  // Read 8 floats

                // Lookup and execute the appropriate parser
                if (row_id >= 0 && row_id <= 62 && lookup_table[row_id] != NULL) {
                    lookup_table[row_id](values);
                } else {
                    printf("Unsupported row_id: %d\n", row_id);
                }

            }

            data_source_set_location(
                DATA_SOURCE(self), &(LocationData){
                    .super.latitude = position.latitude,
                    .super.longitude = position.longitude,
                    .altitude = position.altitude
                }
            );

            data_source_set_dynamics(
                DATA_SOURCE(self), &(DynamicsData){
                    .airspeed = airspeed.indicated_airspeed,
                    .vertical_speed = airspeed.vertical/60,  // it's expecting fps, not fpm
                    .slip_rad = attitude.sideslip
                }
            );

            data_source_set_attitude(

                DATA_SOURCE(self), &(AttitudeData){
                    .roll = attitude.roll,
                    .pitch = attitude.pitch,
                    .heading = attitude.heading
                }
            );

            data_source_set_engine_data(
            DATA_SOURCE(self), &(EngineData){
                .rpm = engine.rpm,
                .fuel_flow = engine.fuelflow,
                .oil_temp = engine.oil_temp,
                .oil_press = engine.oil_press,
                .cht = engine.cht,
                .fuel_px = engine.fuel_press,
                .fuel_qty = engine.fuelQuantity[0] + engine.fuelQuantity[1]
                }
            );

            DATA_SOURCE(self)->has_fix = true;

            // printf("Attitude - Pitch: %f, Roll: %f, Heading: %f\n", 
            //        attitude.pitch, attitude.roll, attitude.heading);
            // printf("Position - Lat: %f, Lon: %f, Alt: %f\n", 
            //        position.latitude, position.longitude, position.altitude);
            // printf("Airspeed - Indicated: %f, True: %f, Ground: %f\n", 
            //        airspeed.indicated_airspeed, airspeed.true_airspeed, airspeed.groundspeed);
            // printf("Engine - RPM: %f, OP: %f\n", 
            //        engine.rpm, engine.oil_press);        
        
        }

    return true;
}
