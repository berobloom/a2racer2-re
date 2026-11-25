// ai_lane.cpp: the lane choice of the computer cars (discoveries "Traffic lane choice and
// following"). The file is named by its asserts' __FILE__. The names in quotes in the traces
// (scan_lane, find_dead_end, find_obstacle_here, ...) are the game's own.
#include "spel.h"

// The game calls every function of this file out of line, even find_dead_end (151 bytes, one
// caller), which VC 5.0 /Ob2 otherwise inlines into scan_lane. Turning automatic inlining off
// for the file gives the game's calls; whether the original did it this way is not known.
#pragma auto_inline(off)

// FUNCTION: SPEL 0x4040b0
void find_dead_end(t_lane_info *p_lane_info, int p_wp, int p_range);
// FUNCTION: SPEL 0x404150
void find_obstacle_here(t_lane_info *p_lane_info, int p_wp, int object, int x);
// FUNCTION: SPEL 0x404600
int find_obstacle(int *obstacle, float *range, int wp, int object);

// Fills one lane's t_lane_info: the dead end within p_range waypoints, the obstacle on this
// waypoint, the nearest one ahead (up to p_range, or to the dead end) and behind (up to 2).
// FUNCTION: SPEL 0x403f60
void scan_lane(t_lane_info *p_lane_info, int p_wp, int p_range, int object, int x)
{
    trace("scan_lane() -> calling 'find_dead_end'\n");
    find_dead_end(p_lane_info, p_wp, p_range);
    trace("Dead End = %d, Range = %d.\n", p_lane_info->m_dead_end, p_lane_info->m_dead_range);
    if (p_lane_info->m_dead_end == 0
        || (p_lane_info->m_dead_end == 1 && p_lane_info->m_dead_range > 0)) {
        trace("scan_lane() -> calling 'find_obstacle_here'\n");
        find_obstacle_here(p_lane_info, p_wp, object, x);
    }
    if (p_lane_info->m_dead_end == 0
        || (p_lane_info->m_dead_end == 1 && p_lane_info->m_dead_range > 1)) {
        trace("scan_lane() -> calling 'find_obstacle_front'\n");
        int l_wp = p_wp;
        int l_range = p_range;
        if (p_lane_info->m_front_obstacle == -1) {
            if (p_lane_info->m_dead_end == 1)
                l_range = p_lane_info->m_dead_range;
            while (l_range) {
                l_wp = waypoints.data[l_wp].next;
                if (l_wp == -1)
                    break;
                if (find_obstacle(&p_lane_info->m_front_obstacle, &p_lane_info->m_front_range,
                                  l_wp, object) == 1)
                    break;
                l_range--;
            }
        }
        trace("scan_lane() -> calling 'find_obstacle_rear'\n");
        l_wp = p_wp;
        int n = 2;
        if (p_lane_info->m_rear_obstacle == -1) {
            while (n) {
                l_wp = waypoints.data[l_wp].prev;
                if (l_wp == -1)
                    break;
                if (find_obstacle(&p_lane_info->m_rear_obstacle, &p_lane_info->m_rear_range,
                                  l_wp, object) == 1)
                    break;
                n--;
            }
        }
    }
}

// Follows the lane up to p_range waypoints (straight on, else lane - 1, else lane + 1):
// m_dead_end 1 and the distance when the links run out, else 0 and -1.
// FUNCTION: SPEL 0x4040b0
void find_dead_end(t_lane_info *p_lane_info, int p_wp, int p_range)
{
    int l_range = p_range;
    trace("find_dead_end(p_lane_info = %08X, p_wp = %d, p_range = %d)\n", p_lane_info, p_wp,
          p_range);
    while (l_range) {
        trace("Checking l_wp = %d.\n", p_wp);
        if (p_wp == -1) {
            p_lane_info->m_dead_end = 1;
            p_lane_info->m_dead_range = p_range - l_range;
            return;
        }
        Waypoint *w = &waypoints.data[p_wp];
        p_wp = w->next;
        if (p_wp == -1)
            p_wp = w->lane_down;
        if (p_wp == -1)
            p_wp = w->lane_up;
        if (p_wp != -1)
            l_range--;
    }
    p_lane_info->m_dead_end = 0;
    p_lane_info->m_dead_range = -1;
}
