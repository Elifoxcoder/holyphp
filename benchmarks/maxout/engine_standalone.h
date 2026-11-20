/* engine_standalone.h — the small shim that lets engine_v2.c and
 * engine_challenge.c build as ordinary programs.
 *
 * While these engines lived in src/hphpc/runtime/hphp_std.c they were
 * builtins: the compiler emitted a call to hpbi_holy_*_start() and the
 * runtime supplied hval, hstr and the helpers below. The engines themselves
 * use nothing else from the language runtime, so this header plus
 * engine_main.c is enough to run them without HolyPHP.
 *
 * Build note: see ENGINES.md.
 */
#ifndef HPHP_ENGINE_STANDALONE_H
#define HPHP_ENGINE_STANDALONE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *data;
    size_t len;
} hstr;

/* An integer, or a borrowed string. The engines only ever read port numbers
 * and one database path, so this is all the value type they need. */
typedef struct {
    int64_t i;
    const char *s;
} hval;

static inline hval hp_of_int(int64_t v) {
    hval r;
    r.i = v;
    r.s = 0;
    return r;
}

static inline int64_t hp_val_to_int(hval v) { return v.i; }

static inline hstr *hp_val_to_str(hval v) {
    static hstr h;
    h.data = v.s ? v.s : "";
    h.len = strlen(h.data);
    return &h;
}

static inline void hp_throw_str(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

#endif
