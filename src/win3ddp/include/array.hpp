// array.hpp: T_ARRAY, the 3D library's array template. The game's copy was
// n:\libraries\a2racerii\win3ddp\versie_1\include\array.hpp: its members are inline, and their
// messages carry that path as __FILE__ (spel.dat 0x4826dc). src/check.sh compiles with that
// directory on the include path (drive n: in the Wine prefix), so __FILE__ is the same.
// Only the members the matched code uses are here. Member names after "T_ARRAY::" are the
// game's own (from the messages: get_new, free_all); init, last and used are ours.
// The __LINE__ uses are put on the game's lines with #line; the others are not known.
#ifndef ARRAY_HPP
#define ARRAY_HPP

#include <string.h>

void trace(const char *fmt, ...);
void fatal_error(const char *fmt, ...);

template <class T> class T_ARRAY {
public:
    T *data;                 // +0x00
    int count;               // +0x04
    int last;                // +0x08 where get_new starts looking
    int state;               // +0x0c 0 empty, 1 allocated
    char *used;              // +0x10 one flag per element
    char name[25];           // +0x14
    char type[25];           // +0x2d

    T_ARRAY(const char *n, const char *t)
    {
        state = 0;
        strncpy(name, n, 25);
        strncpy(type, t, 25);
        if (state)
            free_all();
        data = 0;
        count = 0;
        last = 0;
        trace("T_ARRAY : init  %-16.16s %-16.16s[%d] (no alloc)\n", type, name, count);
        used = 0;
        state = 0;
    }
    ~T_ARRAY()
    {
        free_all();
    }
    void free_all()
    {
        if (state == 1) {
            trace("T_ARRAY : deleting %-16.16s %-16.16s[%d]\n", type, name, count);
            delete data;
        }
        if (state) {
            delete used;
            state = 0;
        }
    }
    int get_new()
    {
        int i;
        if (!state)
#line 185
            fatal_error(" %s(%d) : T_ARRAY::get_new %s[%d] of type %s not initialized\n", __FILE__, __LINE__, name, count, type);
        for (i = last; i < count; i++)
            if (!used[i]) {
                used[i] = 1;
                last = i;
                return i;
            }
        for (i = 0; i < last; i++)
            if (!used[i]) {
                used[i] = 1;
                last = i;
                return i;
            }
#line 206
        fatal_error(" %s(%d) : T_ARRAY::new array %s[0..%d] of type %s : not enough elements\n", __FILE__, __LINE__, name, count, type);
        return -1;
    }
    void init(int n)
    {
        int i;
        if (state)
            free_all();
        if (n < 0)
#line 259
            fatal_error(" %s(%d) : T_ARRAY(construction) illegal array size %s %s[0..%d]\n", __FILE__, __LINE__, type, name, n);
        data = new T[n];
        count = n;
        last = 0;
        used = new char[n];
        trace("T_ARRAY : alloc %-16.16s %-16.16s[%d] ok\n", type, name, count);
        state = 1;
        for (i = 0; i < n; i++)
            used[i] = 0;
    }
};

#endif
