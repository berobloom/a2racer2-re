/* a2_dump -- prints what the C parsers see, so their output can be diffed
 * against the Python tools in a2tools/ (which were validated against the game's
 * own runtime logs). */
#include "archive.h"
#include "cars.h"
#include "mesh.h"
#include "track.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int cmd_archive(const char *img, const char *ind, int list)
{
    a2_archive ar;
    if (a2_archive_open(&ar, img, ind) != 0)
        return 1;
    printf("%s: %s index, %zu entries, img %zu bytes\n", ind,
           ar.kind == A2_IND_NAMED ? "named" : "binary", ar.count, ar.img_size);
    if (list)
        for (size_t i = 0; i < ar.count; i++)
            printf("  [%4zu] %-24s off=%9u size=%9u\n", i, ar.entries[i].name,
                   ar.entries[i].offset, ar.entries[i].size);
    a2_archive_close(&ar);
    return 0;
}

static int cmd_meshes(const char *img, const char *ind)
{
    a2_archive ar;
    if (a2_archive_open(&ar, img, ind) != 0)
        return 1;
    size_t tried = 0, ok = 0;
    unsigned long verts = 0, tris = 0;
    for (size_t i = 0; i < ar.count; i++) {
        const char *n = ar.entries[i].name;
        size_t len = strlen(n);
        if (len < 4 || (strcasecmp(n + len - 4, ".d3d") != 0))
            continue;
        tried++;
        uint32_t size = 0;
        const uint8_t *data = a2_archive_data(&ar, i, &size);
        a2_mesh m;
        if (data && a2_mesh_parse(&m, data, size, n) == 0) {
            ok++;
            verts += m.vertex_count;
            tris += m.triangle_count;
            a2_mesh_free(&m);
        }
    }
    printf("meshes: %zu/%zu parsed, %lu vertices, %lu triangles\n", ok, tried, verts, tris);
    a2_archive_close(&ar);
    return ok == tried ? 0 : 1;
}

static int cmd_track(const char *dir, const char *stem)
{
    a2_track tr;
    if (a2_track_load(&tr, dir, stem) != 0)
        return 1;
    unsigned long per[A2_RMP_LISTS] = {0};
    unsigned long total = 0;
    for (uint32_t i = 0; i < tr.rmp.chunk_count; i++)
        for (int l = 0; l < A2_RMP_LISTS; l++) {
            per[l] += tr.rmp.chunks[i].counts[l];
            total += tr.rmp.chunks[i].counts[l];
        }
    unsigned long rails = 0, wps = 0;
    for (uint32_t i = 0; i < tr.pmp.chunk_count; i++) {
        rails += tr.pmp.chunks[i].rail_count;
        wps += tr.pmp.chunks[i].wp_left_count + tr.pmp.chunks[i].wp_right_count;
    }
    printf("%s/%s\n", dir, stem);
    printf("  chunks            %u\n", tr.rmp.chunk_count);
    printf("  planes            road=%lu land=%lu list2=%lu list3=%lu total=%lu (hint %u)\n",
           per[0], per[1], per[2], per[3], total, tr.rmp.plane_count_hint);
    printf("  rails             %u in .cmp, %lu referenced by chunks\n", tr.rail_count, rails);
    printf("  waypoints         %u in .dmp, %lu referenced by chunks\n", tr.waypoint_count, wps);
    printf("  objects           %u\n", tr.object_count);
    if (tr.object_count)
        printf("  object[0]         chunk=%d type=%d pos=(%.3f, %.3f, %.3f) rot_y=%.3f name=%s\n",
               tr.objects[0].chunk, tr.objects[0].type, tr.objects[0].x, tr.objects[0].y,
               tr.objects[0].z, tr.objects[0].rot_y, tr.objects[0].name);
    unsigned long pts = 0;
    for (uint32_t i = 0; i < tr.path_count; i++)
        pts += tr.paths[i].point_count;
    printf("  scripted paths    %u (version %d), %lu points\n", tr.path_count, tr.path_version,
           pts);
    for (uint32_t i = 0; i < tr.path_count && i < 3; i++)
        printf("  path[%u]           chunks %u..%u type=%u lap=%u time=%ums points=%u\n", i,
               tr.paths[i].first_chunk, tr.paths[i].last_chunk, tr.paths[i].f2, tr.paths[i].f3,
               tr.paths[i].f4, tr.paths[i].point_count);
    /* Trigger census, and the two values the camera system cares about: the
     * posted-limit triggers 4/5/6/8 and the camera trigger 1.  The limits the
     * game writes into 0x48C480 are 50/70/100/120 for 4/5/6/8 respectively. */
    {
        unsigned seen[257] = {0};
        unsigned over = 0;
        for (uint32_t i = 0; i < tr.pmp.chunk_count; i++) {
            uint32_t t[2] = {tr.pmp.chunks[i].trigger0, tr.pmp.chunks[i].trigger1};
            for (int k = 0; k < 2; k++) {
                if (t[k] < 257)
                    seen[t[k]]++;
                else
                    over++;
            }
        }
        printf("  triggers         ");
        for (unsigned v = 1; v < 257; v++)
            if (seen[v])
                printf(" %u x%u", v, seen[v]);
        if (over)
            printf("  (+%u >=257)", over);
        printf("\n");
        static const int lim[4] = {50, 70, 100, 120};
        static const int lv[4] = {4, 5, 6, 8};
        printf("  posted limits    ");
        for (int k = 0; k < 4; k++) {
            int first = -1;
            for (uint32_t i = 0; i < tr.pmp.chunk_count && first < 0; i++)
                if (tr.pmp.chunks[i].trigger0 == (uint32_t)lv[k] ||
                    tr.pmp.chunks[i].trigger1 == (uint32_t)lv[k])
                    first = (int)i;
            printf(" %d km/h(t%d)=%u", lim[k], lv[k], seen[lv[k]]);
            (void)first;
        }
        printf("\n");
        /* The trigger-posted limit in chunk order, against the MAX/MX sign
         * objects, to see whether the painted signs agree with the enforced
         * limit. */
        printf("  limit triggers   ");
        for (uint32_t i = 0; i < tr.pmp.chunk_count; i++) {
            uint32_t t[2] = {tr.pmp.chunks[i].trigger0, tr.pmp.chunks[i].trigger1};
            for (int k = 0; k < 2; k++)
                for (int j = 0; j < 4; j++)
                    if (t[k] == (uint32_t)lv[j])
                        printf(" c%u=%d", i, lim[j]);
        }
        printf("\n  MAX objects      ");
        for (uint32_t i = 0; i < tr.object_count; i++) {
            const char *m = strstr(tr.objects[i].name, "MAX");
            if (!m)
                m = strstr(tr.objects[i].name, "MX");
            if (m)
                printf(" c%u=%s", tr.objects[i].chunk, tr.objects[i].name);
        }
        printf("\n");
    }
    /* Objects near a chunk of interest, for chasing autopilot wedges. */
    {
        const char *want = getenv("A2_DUMP_NEAR");
        if (want) {
            int c0 = atoi(want);
            printf("  near chunk %d:\n", c0);
            for (uint32_t i = 0; i < tr.object_count; i++)
                if ((int)tr.objects[i].chunk >= c0 - 3 && (int)tr.objects[i].chunk <= c0 + 3)
                    printf("    c%-4d type=%d pos=(%.1f, %.1f, %.1f) rot=%.1f %s\n",
                           tr.objects[i].chunk, tr.objects[i].type, tr.objects[i].x,
                           tr.objects[i].y, tr.objects[i].z, tr.objects[i].rot_y,
                           tr.objects[i].name);
            /* Waypoints of that chunk, so an object's distance from the racing
             * line can be read off directly. */
            for (int c = c0 - 1; c <= c0 + 1; c++) {
                if (c < 0 || c >= (int)tr.pmp.chunk_count)
                    continue;
                const a2_pmp_chunk *pc = &tr.pmp.chunks[c];
                for (uint32_t j = 0; j < pc->wp_left_count; j++) {
                    const a2_waypoint *w = &tr.waypoints[pc->wp_left[j]];
                    printf("    wpL c%-4d (%.1f, %.1f)\n", c, w->x, w->z);
                }
                for (uint32_t j = 0; j < pc->wp_right_count; j++) {
                    const a2_waypoint *w = &tr.waypoints[pc->wp_right[j]];
                    printf("    wpR c%-4d (%.1f, %.1f)\n", c, w->x, w->z);
                }
            }
        }
    }
    /* The per-chunk index lists should partition .cmp and .dmp exactly. */
    printf("  partition check   rails %s, waypoints %s\n",
           rails == tr.rail_count ? "OK" : "MISMATCH",
           wps == tr.waypoint_count ? "OK" : "MISMATCH");
    a2_track_free(&tr);
    return 0;
}

static int cmd_cars(const char *dir)
{
    a2_cars c;
    if (a2_cars_load(&c, dir) != 0)
        return 1;
    /* the game's own names (menu.dat 0x2c958); see racers.c */
    static const char *const names[7] = {"Brabant",    "Opol Astro",       "Wolfswagen", "BWM Z3.5",
                                         "PARCHE 913", "Mersedes SLK GTR", "Lambdaghini"};
    printf("%-16s %6s %6s  %5s %5s  %6s %6s %6s   roll(d,f) pitch(d,f) height(d,f)\n", "car",
           "top", "top+", "upsh", "dnsh", "brake", "engdec", "steer");
    for (int i = 0; i < A2_CAR_SLOTS; i++) {
        const a2_car *car = &c.cars[i];
        if (car->empty)
            continue;
        printf("%-16s %5.0f  %5.0f  %5.2f %5.2f  %6.2f %6.2f %6.1f   %4.0f %4.0f  %4.0f %4.0f  %4.0f %4.0f\n",
               i < 7 ? names[i] : "?", a2_car_top_speed(car, 0) * 3.6f,
               a2_car_top_speed(car, 1) * 3.6f, car->upshift_point, car->downshift_point,
               car->brake_force, car->engine_decel, car->steer_speed, car->roll_damping,
               car->roll_factor, car->pitch_damping, car->pitch_factor, car->height_damping,
               car->height_factor);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[1], "archive") == 0)
        return cmd_archive(argv[2], argv[3], argc > 4 && strcmp(argv[4], "--list") == 0);
    if (argc == 4 && strcmp(argv[1], "meshes") == 0)
        return cmd_meshes(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "track") == 0)
        return cmd_track(argv[2], argv[3]);
    if (argc == 3 && strcmp(argv[1], "cars") == 0)
        return cmd_cars(argv[2]);
    fprintf(stderr,
            "usage:\n"
            "  a2_dump archive <IMG> <IND> [--list]\n"
            "  a2_dump meshes  <IMG> <IND>\n"
            "  a2_dump track   <datadir> <etappe0>\n"
            "  a2_dump cars    <datadir>\n"
            "\n"
            "  track also honours A2_DUMP_NEAR=<chunk>, which lists the objects and\n"
            "  driving waypoints within a chunk or two of that chunk.\n");
    return 2;
}
