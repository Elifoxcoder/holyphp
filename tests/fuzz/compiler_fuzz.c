/* compiler_fuzz.c — libFuzzer target for the HolyPHP front end.
 *
 * Feeds arbitrary bytes to the lexer, parser and sema. Invalid programs are
 * the normal case and must only produce diagnostics; the fuzzer looks for
 * crashes, hangs and sanitizer reports. Import resolution and code generation
 * are deliberately not part of this target (they need a file system and a C
 * compiler); see tests/fuzz/README.md.
 *
 * Build and run: bash tests/fuzz/run.sh compiler
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "builtins.h"
#include "diag.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    (void)argv;
    /* diagnostics for random garbage would drown the fuzzer's own output */
    setenv("HPHP_DIAG", "quiet", 1);
    diag_init();
    builtins_init();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* The parser is recursive descent; a megabyte of nested brackets is a
     * timeout, not a bug worth reporting. */
    if (size > 256 * 1024) return 0;

    char *src = (char *)xmalloc(size + 1);
    memcpy(src, data, size);
    src[size] = 0;

    /* A syntax error in this compiler is a fatal(): catch it so the fuzzer
     * keeps going instead of treating expected diagnostics as a crash. */
    jmp_buf jb;
    hp_fatal_jmp = &jb;
    if (setjmp(jb) == 0) {
        Program *prog = parse_program(src, "fuzz.hphp");
        if (prog) {
            prog->file = intern("fuzz.hphp");
            sema_run(prog);
        }
    }
    hp_fatal_jmp = NULL;
    free(src);
    return 0;
}