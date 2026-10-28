#ifndef ROADHAUL_SIM_H
#define ROADHAUL_SIM_H
#include <stdint.h>
#define RH_ROAD_SPACING 1800.0f
#define RH_CITY_BLOCK 480.0f
#define RH_CITY_BLOCKS 10
#define RH_CITY_SPACING 14400.0f
#define RH_MAX_TRAFFIC 8
#define RH_SAVE_VERSION 10
#define RH_BRIDGE_HALF_LENGTH 550.0f
#define RH_BRIDGE_HALF_WIDTH 6.5f
#define RH_TRAILER_HITCH 2.3f
#define RH_TRAILER_AXLE 7.5f
#define RH_TRAILER_CENTER 4.2f
#define RH_PI 3.14159265358979323846f

/* ===== STATE MACHINE (TZ 2.0) ===== */
typedef enum {
    ST_MENU = 0,
    ST_GARAGE,
    ST_COMPUTER,
    ST_PLAY,
    ST_PHONE,
    ST_UNLOAD,
    ST_RADIO,
    ST_PAUSE
} GameState;

typedef struct { const char *name; float mass_kg, wheelbase_m, torque_nm, gears[8];
    int n_gears; float final_drive, wheel_radius, tank_l, max_steer_rad, price; float length_m; } TruckSpec;

typedef struct {
    float steer, throttle, brake;
    int handbrake, reverse_request, action, swap_truck, toggle_camera, pause, map, look;
    int menu_up, menu_down, menu_left, menu_right, menu_accept;
    float cam_orbit;
    float cam_zoom;
} Controls;

typedef struct {
    float x,z,yaw,v,speed_abs, rpm, fuel, odo, damage, trailer_yaw;
    float engine_t, gearbox_t; int gear, model, camera, reverse;
    float trailer_x,trailer_z,lateral_v,fuel_pending; int road_level;
} Player;

typedef struct { float x,z,yaw,speed; int axis, direction, variant; } Traffic;

typedef struct {
    int pickup_x, pickup_z, dest_x, dest_z, stage, cargo_tons, type;
    float distance_km, payout;
} Job;

typedef struct {
    Player player;
    Job job;
    Traffic cars[RH_MAX_TRAFFIC];
    int traffic_count, paused, map_open, completed, last_notice, notice_ticks;
    unsigned int owned_trucks;
    float bank, drive_time, traffic_t, sim_t, clock_hours;
    uint32_t rng;
    int weather, weather_mode, time_mode, settings_row;
    float weather_next_odo;
    int service_phase;
    float service_timer, service_x, service_z, service_yaw;
    int transport_mode, on_foot;
    float person_x, person_z, person_yaw;
    int person_level, parked_valid, parked_level;
    float parked_x, parked_z, parked_yaw, parked_trailer_yaw;

    /* NEW 2.0 */
    GameState state;
    float garage_cam_yaw;
    float garage_cam_dist;
    float garage_cam_height;
    int   computer_sel;
    int   unload_sel;
    int   phone_sel;
    int   menu_blink;
    float menu_timer;
    int   radio_station;  /* 0..3 */
    int   radio_open;
    float approach_dot; /* for reverse parking bonus */
    int   home_city;
    int   current_city;
} Game;

extern const TruckSpec RH_TRUCKS[3];

void rh_new_game(Game *g);
void rh_step(Game *g, Controls in, float dt);
void rh_set_player(Game *g, float x, float z, float yaw);
float rh_height(float x, float z);
int rh_biome(float x, float z);
int rh_bridge_info(int gx, int gz, float *x, float *z);
float rh_bridge_deck(int gx, int gz, float z);
float rh_road_height(float x, float z, int axis);
float rh_vehicle_height(float x, float z, float yaw);
float rh_player_height(const Game *g);
float rh_road_distance(float x, float z);
float rh_road_x(int gx, float z);
float rh_road_z(int gz, float x);
int rh_settlement_kind(int gx, int gz);
int rh_city_tile(int tx, int tz);
float rh_cityroad_distance(float x, float z);
int rh_tree_at(int cellx, int cellz, float *x, float *z);
int rh_water(float x, float z);
void rh_depot(int gx, int gz, float *x, float *z);
void rh_station(int gx, int gz, float *x, float *z);
float rh_dist2(float x, float z, float x2, float z2);
float rh_job_target_distance(const Game *g);
void rh_waypoint(const Game *g, float *x, float *z);
int rh_save(const Game *g, const char *path);
int rh_load(Game *g, const char *path);

void rh_enter_garage(Game *g);
void rh_leave_garage(Game *g);
const char *rh_city_name(int idx);
int rh_nearest_city(float x, float z);

#endif
