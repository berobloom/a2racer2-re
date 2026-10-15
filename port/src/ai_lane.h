/* What every AI driver sees of its lanes: the original's ai_lane.cpp
 * (change_lane 0x403660).  Each tick, per lane of the driver's chunk, the
 * nearest vehicle on the first waypoint ahead holding one, scanning up to 10
 * waypoints (ai +0x10 = 10, 0x406a13), and the same behind over 2
 * (scan_lane 0x403f60, find_obstacle 0x404600; the range is the 3D
 * distance).  Statuses as 0x404720 sets them: 0 nothing, 1 faster than us,
 * 2 not faster (3/4 dead ends and 5 oncoming do not occur: each
 * carriageway's lanes are one-way, and a vehicle is only seen by those on its
 * own carriageway).  See research/discoveries.md "Traffic lane choice and
 * following". */
#ifndef A2_AI_LANE_H
#define A2_AI_LANE_H

typedef struct {
    float x, y, z, speed; /* world units, m/s */
    int chunk, lane;
    float t;              /* along its segment, for "ahead" within one chunk */
    int oncoming;         /* on the other carriageway (side 0), driven against
                           * the chunk order */
} a2_ai_vehicle;

typedef struct {
    int front, rear;               /* status, 0 = nothing */
    float front_range, rear_range; /* world units */
    float front_speed;             /* the obstacle's speed, m/s */
} a2_lane_info;

#define A2_AI_SCAN_AHEAD 10
#define A2_AI_SCAN_BEHIND 2

/* `self` must not be in `v` (or is skipped by position). */
void a2_ai_scan_lanes(const a2_ai_vehicle *self, const a2_ai_vehicle *v, int nv, int lanes,
                      a2_lane_info *L);

/* 0x4055c0: the chosen speed for a lane, from behaviour +0x14 preferred,
 * +0x20 close and +0x24 follow ranges and the current chosen speed. */
float a2_ai_lane_speed(const a2_lane_info *l, float preferred, float close, float follow,
                       float speed);

/* race_lane, 0x404ee0: the racers' choice, with behaviour +0x1c (rear gap)
 * and +0x24 (front gap). */
int a2_ai_race_lane(const a2_lane_info *L, int lanes, int cur, float rear_gap, float front_gap);

#endif
