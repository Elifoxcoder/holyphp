/* engine_main.c — run an archived MaxOut/challenge engine as a plain server.
 *
 *   gcc -O2 engine_v2.c engine_main.c -lsqlite3 -o holy_server
 *   gcc -O2 -DENGINE_CHALLENGE engine_challenge.c engine_main.c \
 *       -lsqlite3 -o holy_challenge_server
 *
 * PORT and DB_PATH are read from the environment, the same way bench.sh
 * starts the servers. JWT_SECRET is read by the challenge engine itself.
 */
#include <stdio.h>
#include <stdlib.h>

#include "engine_standalone.h"

#if defined(ENGINE_CHALLENGE)
hval hpbi_holy_challenge_start(hval port, hval db_path);
#else
hval hpbi_holy_maxout_start(hval port, hval db_path);
#endif

int main(void) {
    const char *port = getenv("PORT");
    const char *db = getenv("DB_PATH");
    hval p, d;

    p.i = port && *port ? atoi(port) : 0;
    p.s = 0;
    d.i = 0;
    d.s = db && *db ? db : 0;

#if defined(ENGINE_CHALLENGE)
    hval rc = hpbi_holy_challenge_start(p, d);
#else
    hval rc = hpbi_holy_maxout_start(p, d);
#endif
    if (rc.i < 0) return 1;
    return 0;
}
