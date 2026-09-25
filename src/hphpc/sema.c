/* sema.c — semantic analysis for HolyPHP.
 *
 * Implements:
 *   - name resolution (functions, classes, enums, variables, methods)
 *   - type inference & checking with PHP-style numeric promotion
 *   - the memory model:
 *       * value semantics for scalars/arrays (PHP-like, COW at runtime)
 *       * &x / &mut x borrows, checked like Rust: many readers XOR one writer
 *       * own<T> moves, Rc<T> shared smart pointers
 *       * raw pointers *T only inside unsafe blocks / unsafe fns
 */
#include "sema.h"
#include "builtins.h"
#include <stdarg.h>

/* ---------------- global symbol tables ---------------- */
typedef struct FnSym {
    const char *name;
    AstFn *fn;
    const FnSig *sig;
    struct FnSym *next;
} FnSym;

typedef struct ClassSym {
    const char *name;
    AstClass *cls;
    struct ClassSym *next;
} ClassSym;

typedef struct EnumSym {
    const char *name;
    AstEnum *enm;
    const Type *type;
    struct EnumSym *next;
} EnumSym;

static FnSym *fns = NULL;
static ClassSym *classes = NULL;
static EnumSym *enums = NULL;
static bool sema_ok = true;
static size_t var_uid = 0;

/* scopes */
#define MAX_SCOPE 64
typedef struct Scope {
    VarSym *vars[MAX_SCOPE];
    size_t nvars;
    struct Scope *parent;
    AstFn *cur_fn;
    AstClass *cur_class;
    bool in_unsafe;
    int loop_depth;
    const Type *self_type;
    int seq;                   /* creation order, for capture analysis */
} Scope;

static void check_stmt(Scope *sc, Stmt *s);
static const Type *check_expr(Scope *sc, Expr *e);

/* ---- closure capture collection ----
 * While checking a closure body, every variable that resolves OUTSIDE the
 * closure is recorded as a capture (by value, PHP-like). Scope structs get
 * sequence numbers; a var is "outer" iff it was declared in a scope with a
 * smaller seq than the closure body scope. Nested closures propagate their
 * captures (and this-ness) outward. */
static AstFn *g_clo_fn[32];
static int g_clo_body_seq[32];
static PtrVec g_clo_caps[32];
static bool g_clo_this[32];
static int g_clo_depth = 0;
static int g_scope_seq = 0;

/* Program-wide registry for `global $x;` variables. All ST_GLOBAL stmts
 * (in any function) resolve to the SAME VarSym so codegen emits one file-
 * scope definition per name and every use shares it. */
static PtrVec g_program_globals = {0};
static VarSym *program_global_find(const char *name) {
    for (size_t i = 0; i < g_program_globals.len; i++) {
        VarSym *v = g_program_globals.items[i];
        if (v->name == name) return v;   /* names are interned */
    }
    return NULL;
}
static VarSym *program_global_get(const char *name, Token tok) {
    VarSym *v = program_global_find(name);
    if (!v) {
        v = xcalloc(1, sizeof(VarSym));
        v->name = name;
        v->type = ty_mixed;
        v->tok = tok;
        v->is_mut = true;
        v->is_auto = true;
        v->is_program_global = true;
        v->uid = (int)var_uid++;   /* unique C name for the file-scope storage */
        ptrvec_push(&g_program_globals, v);
    }
    return v;
}

static void note_capture(VarSym *v) {
    PtrVec *list = &g_clo_caps[g_clo_depth - 1];
    for (size_t i = 0; i < list->len; i++)
        if (list->items[i] == v) return;
    ptrvec_push(list, v);
    v->captured = true;
}

void sema_error(const Token *tok, const char *fmt, ...) {
    sema_ok = false;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%zu:%zu: error: ", tok ? tok->file : "?", tok ? tok->line : 0, tok ? tok->col : 0);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

bool sema_result_ok(void) { return sema_ok; }

/* ---------------- type helpers ---------------- */
static const Type *unify_numeric(const Type *a, const Type *b) {
    if (!a || !b) return ty_mixed;
    bool af = a->kind == TY_FLOAT || a->kind == TY_F32 || a->kind == TY_F64;
    bool bf = b->kind == TY_FLOAT || b->kind == TY_F32 || b->kind == TY_F64;
    if (af || bf) {
        if (a->kind == TY_F32 && b->kind == TY_F32) return ty_f32;
        if ((a->kind == TY_F64 || b->kind == TY_F64)) return ty_f64;
        return ty_float;
    }
    if (a->kind == TY_INT && b->kind == TY_INT) return ty_int;
    /* mixed-size ints: widen to i64 */
    if (type_is_integral(a->kind) && type_is_integral(b->kind)) {
        if (a->kind == TY_I8 && b->kind == TY_I8) return ty_i8;
        if (a->kind == TY_I16 && b->kind == TY_I16) return ty_i16;
        if (a->kind == TY_I32 && b->kind == TY_I32) return ty_i32;
        return ty_i64;
    }
    return ty_mixed;
}

static bool types_compatible(const Type *want, const Type *got);

/* does this statement (or any statement nested in it) return a value? */
static bool stmt_chain_returns(Stmt *s) {
    if (!s) return false;
    switch (s->kind) {
    case ST_RETURN: return s->u.ret.value != NULL;
    case ST_IF: {
        for (size_t i = 0; i < s->u.if_.then.len; i++)
            if (stmt_chain_returns(s->u.if_.then.items[i])) return true;
        for (size_t i = 0; i < s->u.if_.els.len; i++)
            if (stmt_chain_returns(s->u.if_.els.items[i])) return true;
        return false;
    }
    case ST_WHILE:
    case ST_DO:
        for (size_t i = 0; i < s->u.while_.body.len; i++)
            if (stmt_chain_returns(s->u.while_.body.items[i])) return true;
        return false;
    case ST_FOR:
        for (size_t i = 0; i < s->u.for_.body.len; i++)
            if (stmt_chain_returns(s->u.for_.body.items[i])) return true;
        return false;
    case ST_FOREACH:
        for (size_t i = 0; i < s->u.foreach.body.len; i++)
            if (stmt_chain_returns(s->u.foreach.body.items[i])) return true;
        return false;
    case ST_TRY:
        for (size_t i = 0; i < s->u.try.body.len; i++)
            if (stmt_chain_returns(s->u.try.body.items[i])) return true;
        return false;
    case ST_BLOCK:
        for (size_t i = 0; i < s->u.block.stmts.len; i++)
            if (stmt_chain_returns(s->u.block.stmts.items[i])) return true;
        return false;
    default:
        return false;
    }
}

static bool types_compatible(const Type *want, const Type *got);

static bool types_equal(const Type *a, const Type *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) {
        /* int fits any int width by promotion, treat widths loosely */
        if (type_is_integral(a->kind) && type_is_integral(b->kind)) return true;
        if (type_is_numeric(a->kind) && type_is_numeric(b->kind)) return true;
        if (a->kind == TY_STRING && b->kind == TY_STRING) return true;
        return false;
    }
    switch (a->kind) {
    case TY_ARRAY: case TY_VEC: case TY_SET: case TY_OPTION:
    case TY_REF: case TY_MUTREF: case TY_PTR: case TY_OWN: case TY_RC:
        /* PHP-style: a plain `array` annotation is generic; accept any
         * element type written into the annotation or inferred. */
        if (a->kind == TY_ARRAY && a->elem && a->elem->kind == TY_MIXED) return true;
        return types_equal(a->elem, b->elem);
    case TY_MAP:
        return types_equal(a->key, b->key) && types_equal(a->val, b->val);
    case TY_RESULT:
        return types_equal(a->ok, b->ok) && types_equal(a->err, b->err);
    case TY_FN: {
        if (a->fn.nparams != b->fn.nparams) return false;
        for (size_t i = 0; i < a->fn.nparams; i++)
            if (!types_equal(a->fn.params[i], b->fn.params[i])) return false;
        return types_equal(a->fn.ret, b->fn.ret);
    }
    case TY_CLASS: case TY_STRUCT: case TY_INTERFACE: case TY_ENUM:
        return a->name == b->name;
    default:
        return true;
    }
}

static bool types_compatible(const Type *want, const Type *got) {
    if (!want || want->kind == TY_MIXED) return true;
    if (!got) return true;
    /* PHP coercive typing: a dynamically typed value (untyped property, map
     * entry, mixed param) converts to any declared type at the boundary —
     * codegen emits the hp_val_to_* conversion. */
    if (got->kind == TY_MIXED || got->kind == TY_OPTION || got->kind == TY_RESULT)
        return true;
    /* `callable`/variadic fn types accept any closure */
    if (want->kind == TY_FN && want->fn.variadic && got->kind == TY_FN) return true;
    if (got->kind == TY_FN && got->fn.variadic) return true;
    if (types_equal(want, got)) return true;
    /* numeric widening */
    if (type_is_numeric(want->kind) && type_is_numeric(got->kind)) return true;
    /* returning/passing a fresh object: own<T> coerces to T (ownership
     * transfers to the receiving slot; codegen already stores raw structs) */
    if ((want->kind == TY_CLASS || want->kind == TY_STRUCT) &&
        got->kind == TY_OWN && got->elem &&
        (got->elem->kind == TY_CLASS || got->elem->kind == TY_STRUCT) &&
        got->elem->name == want->name)
        return true;
    if (want->kind == TY_OWN && got->kind == TY_CLASS &&
        want->elem && want->elem->name == got->name)
        return true;
    /* &T accepts T (auto-borrow) */
    if (want->kind == TY_REF && types_compatible(want->elem, got)) return true;
    if (want->kind == TY_MUTREF && got->kind != TY_MUTREF) return false;
    return false;
}

/* ---------------- scopes ---------------- */
static void scope_init(Scope *s, Scope *parent) {
    memset(s, 0, sizeof(*s));
    s->parent = parent;
    s->seq = g_scope_seq++;
}

static VarSym *scope_find(Scope *s, const char *name) {
    for (Scope *sc = s; sc; sc = sc->parent) {
        for (size_t i = sc->nvars; i > 0; i--) {
            VarSym *v = sc->vars[i - 1];
            if (v->name == intern(name)) return v;
        }
    }
    return NULL;
}

static VarSym *scope_declare(Scope *s, const char *name, const Type *t, Token tok, bool is_mut) {
    if (s->nvars >= MAX_SCOPE) {
        sema_error(&tok, "too many variables in one scope");
        return NULL;
    }
    VarSym *v = xcalloc(1, sizeof(VarSym));
    v->name = intern(name);
    v->type = t;
    v->tok = tok;
    v->is_mut = is_mut;
    v->uid = (int)var_uid++;
    v->decl_seq = s->seq;
    s->vars[s->nvars++] = v;
    return v;
}

/* ---------------- forward decls ---------------- */
static const Type *check_expr(Scope *sc, Expr *e);
static void check_stmt(Scope *sc, Stmt *s);
static const Type *resolve_type(Scope *sc, Type *t, Token tok);
static void check_fn_body(Scope *parent, AstFn *fn, const Type *self_type);
static bool fn_has_value_return(AstFn *fn);
/* number of required params: params after the first defaulted one are optional */
static size_t fn_min_args(AstFn *fn) {
    size_t n = 0;
    for (size_t i = 0; i < fn->params.len; i++) {
        Param *pm = fn->params.items[i];
        if (!pm->dflt) n = i + 1;
    }
    return n;
}
static const Type *check_builtin_call(Scope *sc, Expr *e, const Builtin *b);
static const char *expr_kind_name(ExprKind k);
static const char *s_this;
static const char *s_argc;
static const char *s_argv;

/* ---------------- declarations pass ---------------- */
static AstFn *class_find_method(AstClass *c, const char *name) {
    for (size_t i = 0; i < c->methods.len; i++) {
        AstFn *m = c->methods.items[i];
        if (m->name == intern(name)) return m;
    }
    return NULL;
}

static StructField *class_find_field(AstClass *c, const char *name) {
    for (AstClass *cc = c; cc; cc = cc->base)
        for (size_t i = 0; i < cc->fields.len; i++) {
            StructField *f = cc->fields.items[i];
            if (f->name == intern(name)) {
                if (!f->cls) f->cls = cc;   /* record defining class once */
                return f;
            }
        }
    return NULL;
}

static Type *class_type_of(AstClass *c) {
    Type *t = type_new(c->is_interface ? TY_INTERFACE : TY_CLASS);
    t->name = intern(c->name);
    t->decl = c;
    return t;
}

static Type *enum_type_of(AstEnum *e) {
    Type *t = type_new(TY_ENUM);
    t->name = intern(e->name);
    return t;
}

static const Type *sym_type_of_enum(AstEnum *e) {
    for (EnumSym *s = enums; s; s = s->next)
        if (s->enm == e) return s->type;
    return enum_type_of(e);
}

static FnSig *fn_make_sig(AstFn *fn) {
    FnSig *sig = xcalloc(1, sizeof(FnSig));
    sig->nparams = fn->params.len;
    sig->params = xmalloc(sizeof(Type *) * (fn->params.len ? fn->params.len : 1));
    sig->is_unsafe = fn->is_unsafe;
    sig->ret = fn->ret; /* may be NULL: inferred later */
    return sig;
}

static void declare_decls(Program *prog) {
    /* functions */
    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *d = prog->decls.items[i];
        if (d->kind != DK_FN) continue;
        AstFn *fn = d->u.fn;
        if (fn->name == intern("main")) {
            if (fn->params.len != 0)
                sema_error(&fn->name_tok, "main() takes no parameters");
            if (fn->ret && fn->ret->kind != TY_VOID && fn->ret->kind != TY_INT)
                sema_error(&fn->name_tok, "main() returns void or int");
        }
        FnSym *sym = xcalloc(1, sizeof(FnSym));
        sym->name = intern(fn->name);
        sym->fn = fn;
        sym->sig = fn_make_sig(fn);
        sym->next = fns;
        fns = sym;
        fn->ftype = type_new(TY_FN);
    }
    /* classes / interfaces / traits */
    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *d = prog->decls.items[i];
        if (d->kind != DK_CLASS && d->kind != DK_INTERFACE && d->kind != DK_TRAIT) continue;
        AstClass *c = d->u.cls;
        ClassSym *sym = xcalloc(1, sizeof(ClassSym));
        sym->name = intern(c->name);
        sym->cls = c;
        sym->next = classes;
        classes = sym;
    }
    /* enums */
    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *d = prog->decls.items[i];
        if (d->kind != DK_ENUM) continue;
        AstEnum *en = d->u.enm;
        EnumSym *sym = xcalloc(1, sizeof(EnumSym));
        sym->name = intern(en->name);
        sym->enm = en;
        sym->type = enum_type_of(en);
        sym->next = enums;
        enums = sym;
    }
    /* resolve bases */
    for (ClassSym *cs = classes; cs; cs = cs->next) {
        AstClass *c = cs->cls;
        if (c->extends) {
            for (ClassSym *b = classes; b; b = b->next) {
                if (b->name == intern(c->extends)) {
                    c->base = b->cls;
                    break;
                }
            }
            if (!c->base)
                sema_error(&c->name_tok, "unknown base class '%s'", c->extends);
        }
    }
}

static AstFn *find_function(const char *name) {
    for (FnSym *s = fns; s; s = s->next)
        if (s->name == intern(name)) return s->fn;
    return NULL;
}

static AstClass *find_class(const char *name) {
    for (ClassSym *s = classes; s; s = s->next)
        if (s->name == intern(name)) return s->cls;
    return NULL;
}

static AstEnum *find_enum(const char *name) {
    for (EnumSym *s = enums; s; s = s->next)
        if (s->name == intern(name)) return s->enm;
    return NULL;
}

static bool class_derives(AstClass *c, const AstClass *base) {
    for (AstClass *b = c; b; b = b->base)
        if (b == base) return true;
    return false;
}

/* ---------------- type resolution ---------------- */
static const Type *resolve_type(Scope *sc, Type *t, Token tok) {
    (void)sc;
    if (!t) return NULL;
    switch (t->kind) {
    case TY_ARRAY: t->elem = resolve_type(sc, (Type *)t->elem, tok); break;
    case TY_VEC: case TY_SET: case TY_OPTION:
        t->elem = resolve_type(sc, (Type *)t->elem, tok); break;
    case TY_MAP:
        t->key = resolve_type(sc, (Type *)t->key, tok);
        t->val = resolve_type(sc, (Type *)t->val, tok); break;
    case TY_RESULT:
        t->ok = resolve_type(sc, (Type *)t->ok, tok);
        t->err = resolve_type(sc, (Type *)t->err, tok); break;
    case TY_REF: case TY_MUTREF: case TY_PTR: case TY_OWN: case TY_RC:
        t->elem = resolve_type(sc, (Type *)t->elem, tok); break;
    case TY_FN: {
        for (size_t i = 0; i < t->fn.nparams; i++)
            ((Type **)t->fn.params)[i] = resolve_type(sc, (Type *)t->fn.params[i], tok);
        t->fn.ret = resolve_type(sc, (Type *)t->fn.ret, tok);
        break;
    }
    case TY_CLASS: {
        if (strcmp(t->name, "self") == 0 && sc && sc->self_type) return sc->self_type;
        /* builtin Exception type (thrown by the runtime, caught by user code) */
        if (strcmp(t->name, "Exception") == 0) return t;
        AstClass *c = find_class(t->name);
        if (c) {
            t->decl = c;
            t->name = intern(c->name);
        } else {
            AstEnum *en = find_enum(t->name);
            if (en) {
                t->kind = TY_ENUM;
                t->name = intern(en->name);
            } else {
                sema_error(&tok, "unknown type '%s'", t->name);
                t->kind = TY_ERROR;
            }
        }
        break;
    }
    default: break;
    }
    return t;
}

/* ---------------- borrow checker ---------------- */
#define MAX_LOANS 64
typedef struct Loan {
    const VarSym *var;     /* borrowed variable */
    bool is_mut;
    int from_expr;         /* expression uid that created the loan (scope path id) */
    size_t line;
    bool released;
} Loan;

static Loan loans[MAX_LOANS];
static size_t nloans = 0;
static int loan_scope_id = 1;

static void loans_release_all(void) {
    nloans = 0;
}
static void loans_push(const VarSym *var, bool is_mut, size_t line) {
    if (nloans >= MAX_LOANS) return;
    loans[nloans++] = (Loan){ var, is_mut, loan_scope_id, line, false };
}
static void loans_pop_scope(void) {
    for (size_t i = nloans; i > 0; i--) {
        if (loans[i - 1].from_expr == loan_scope_id) nloans = i - 1;
        else break;
    }
}

static const Type *sym_type_of_enum(AstEnum *e);

/* how many live loans of var; returns bit 1 = shared, bit 2 = mut */
static int loans_of(const VarSym *var) {
    int r = 0;
    for (size_t i = 0; i < nloans; i++)
        if (!loans[i].released && loans[i].var == var)
            r |= loans[i].is_mut ? 2 : 1;
    return r;
}

/* ---------------- expression checking ---------------- */
static const Type *deref_pointerish(const Type *t) {
    if (!t) return NULL;
    if (t->kind == TY_REF || t->kind == TY_MUTREF || t->kind == TY_PTR)
        return t->elem;
    return t;
}

static const Type *check_bin(Scope *sc, Expr *e) {
    const Type *lt = check_expr(sc, e->u.bin.l);
    const Type *rt = check_expr(sc, e->u.bin.r);
    TokKind op = e->u.bin.op.kind;
    /* null coalescing */
    if (e->is_nullcoal) {
        e->type = lt;
        return e->type;
    }
    /* string concat */
    if (op == T_DOT) {
        e->type = ty_string;
        return e->type;
    }
    /* comparisons */
    if (op == T_EQ || op == T_NEQ || op == T_NEQ2 || op == T_EQ2 || op == T_LT ||
        op == T_GT || op == T_LE || op == T_GE || op == T_SPACESHIP) {
        if (type_is_numeric(lt->kind) && type_is_numeric(rt->kind))
            unify_numeric(lt, rt);
        e->type = ty_bool;
        return e->type;
    }
    /* logical */
    if (op == T_ANDAND || op == T_OROR) {
        e->type = ty_bool;
        return e->type;
    }
    /* shifts << >> — the result is always an integer; numeric strings and
     * hval operands are converted by the runtime (PHP semantics) */
    if (op == T_SHL || op == T_SHR) {
        bool lok = type_is_numeric(lt->kind) || lt->kind == TY_STRING || lt->kind == TY_MIXED;
        bool rok = type_is_numeric(rt->kind) || rt->kind == TY_STRING || rt->kind == TY_MIXED;
        if (!lok || !rok)
            sema_error(&e->u.bin.op, "%s requires numeric operands, got %s and %s",
                       tok_kind_name(op), type_to_string(lt), type_to_string(rt));
        e->type = ty_int;
        return e->type;
    }
    /* bitwise */
    if (op == T_AMP || op == T_PIPE || op == T_CARET) {
        if (!type_is_integral(lt->kind) || !type_is_integral(rt->kind)) {
            sema_error(&e->u.bin.op, "bitwise operator requires integer operands, got %s and %s",
                       type_to_string(lt), type_to_string(rt));
        }
        e->type = unify_numeric(lt, rt);
        return e->type;
    }
    /* arithmetic */
    if (op == T_PLUS || op == T_MINUS || op == T_STAR || op == T_SLASH ||
        op == T_PERCENT || op == T_POW) {
        if (type_is_numeric(lt->kind) && type_is_numeric(rt->kind)) {
            e->type = op == T_SLASH ? ty_float : unify_numeric(lt, rt);
            return e->type;
        }
        /* PHP numeric-string arithmetic: "1" + "2" is 3, "1.5" * 2 is
         * 3.0, "abc" + "x" (no numeric meaning) stays our friendly
         * concatenation. The runtime picks int vs float from the values,
         * so the static type is hval — not a guaranteed hstr*. */
        if (op != T_PERCENT && op != T_POW &&
            (lt->kind == TY_STRING || rt->kind == TY_STRING) &&
            (type_is_numeric(lt->kind) || lt->kind == TY_STRING) &&
            (type_is_numeric(rt->kind) || rt->kind == TY_STRING)) {
            e->type = ty_mixed;
            return e->type;
        }
        if (lt->kind == TY_MIXED || rt->kind == TY_MIXED) {
            /* PHP-style: mixed operands (e.g. untyped closure params)
             * flow through the generic runtime arithmetic. */
            e->type = op == T_SLASH ? ty_float : ty_mixed;
            return e->type;
        }
        sema_error(&e->u.bin.op, "unsupported operand types for '%s': %s and %s",
                   tok_kind_name(op), type_to_string(lt), type_to_string(rt));
        e->type = ty_mixed;
        return e->type;
    }
    /* slice a[lo..hi] */
    if (e->is_slice) {
        e->type = lt;
        return e->type;
    }
    e->type = ty_mixed;
    return e->type;
}

static const Type *elem_type_of(const Type *t, const Type *idx) {
    if (!t) return ty_mixed;
    switch (t->kind) {
    case TY_ARRAY: case TY_VEC: case TY_SET:
        return t->elem;
    case TY_MAP:
        return t->val;
    case TY_STRING:
        return ty_string;
    case TY_TUPLE:
        return ty_mixed;
    default:
        (void)idx;
        return NULL;
    }
}

static const Type *check_method_call(Scope *sc, Expr *e) {
    Expr *obj = e->u.method.obj;
    const Type *ot = check_expr(sc, obj);
    const char *mname = e->u.method.name;
    /* builtin Exception support: getMessage() */
    if (ot->kind == TY_CLASS && ot->name == intern("Exception")) {
        if (strcmp(mname, "getMessage") == 0) {
            e->builtin = "exception_get_message";
            e->type = ty_string;
            return e->type;
        }
    }
    if (ot->kind == TY_ARRAY || ot->kind == TY_VEC || ot->kind == TY_MAP || ot->kind == TY_SET) {
        /* collection builtins */
        static const struct { const char *n; const Type *ret; } coll[] = {
            {"len", NULL}, {"push", NULL}, {"pop", NULL},
            {"get", NULL}, {"has", NULL}, {"keys", NULL}, {"values", NULL},
            {NULL, NULL},
        };
        (void)coll;
        if (strcmp(mname, "len") == 0) { e->builtin = "count"; e->type = ty_int; return e->type; }
        if (strcmp(mname, "push") == 0) { e->builtin = "array_push"; e->type = ty_int; return e->type; }
        if (strcmp(mname, "pop") == 0) { e->builtin = "array_pop"; e->type = elem_type_of(ot, NULL); return e->type ? e->type : ty_mixed; }
        if (strcmp(mname, "get") == 0) { e->builtin = "array_get"; e->type = elem_type_of(ot, NULL) ? elem_type_of(ot, NULL) : ty_mixed; return e->type; }
        if (strcmp(mname, "has") == 0) { e->builtin = "array_key_exists"; e->type = ty_bool; return e->type; }
        if (strcmp(mname, "keys") == 0) { e->builtin = "array_keys"; e->type = ty_mixed; return e->type; }
        if (strcmp(mname, "values") == 0) { e->builtin = "array_values"; e->type = ty_mixed; return e->type; }
    }
    /* smart pointer auto-deref */
    if (ot->kind == TY_OWN || ot->kind == TY_RC || ot->kind == TY_REF || ot->kind == TY_MUTREF) {
        const Type *inner = ot->elem;
        if (inner && (inner->kind == TY_CLASS || inner->kind == TY_STRUCT)) {
            AstClass *c = inner->decl;
            if (!c) {
                AstClass *cc = find_class(inner->name);
                inner = NULL;
                if (cc) {
                    ((Type *)ot->elem)->decl = cc;
                    inner = ot->elem;
                    c = cc;
                }
            }
            if (c) {
                AstFn *m = class_find_method(c, mname);
                if (!m && c->base) m = class_find_method(c->base, mname);
                if (m) {
                    e->u.method.target = m;
                    /* check args against params */
                    Scope dummy;
                    scope_init(&dummy, NULL);                     size_t na = e->u.method.args.len;
                     size_t nreq = fn_min_args(m);
                     if (na < nreq || na > m->params.len)
                         sema_error(&e->u.method.name_tok, "method %s::%s() expects %zu..%zu argument(s), got %zu",
                                    c->name, mname, nreq, m->params.len, na);
                    for (size_t i = 0; i < na && i < m->params.len; i++) {
                        Param *pm = m->params.items[i];
                        const Type *pt = resolve_type(sc, pm->type, pm->name_tok);
                        Expr *arg = e->u.method.args.items[i];
                        const Type *at = check_expr(sc, arg);
                        if (pt && !types_compatible(pt, at))
                            sema_error(&arg->tok, "argument %zu to %s::%s(): expected %s, got %s",
                                       i + 1, c->name, mname, type_to_string(pt), type_to_string(at));
                    }
                    if (!m->ret) m->ret = fn_has_value_return(m) ? ty_mixed : ty_void;
                    e->type = resolve_type(sc, m->ret, m->name_tok);
                    return e->type;
                }
            }
        }
    }
    /* plain object */
    if (ot->kind == TY_CLASS || ot->kind == TY_STRUCT) {
        AstClass *c = ot->decl;
        if (!c) c = find_class(ot->name);
        if (!c) {
            sema_error(&e->u.method.name_tok, "call to method '%s' on unknown type %s",
                       mname, type_to_string(ot));
            e->type = ty_mixed;
            return e->type;
        }
        AstFn *m = class_find_method(c, mname);
        if (!m && c->base) m = class_find_method(c->base, mname);
        if (!m) {
            /* PHP-style callable property: $this->onMessage($client, $msg).
             * No method of that name — if a field holds a closure or an
             * untyped value, this is a dynamic call. */
            StructField *f = class_find_field(c, mname);
            if (!f && c->base) f = class_find_field(c->base, mname);
            if (f) {
                for (size_t i = 0; i < e->u.method.args.len; i++)
                    check_expr(sc, e->u.method.args.items[i]);
                e->u.method.sf = f;
                e->u.method.target = NULL;
                e->type = (f->type && f->type->kind == TY_FN && f->type->fn.ret)
                              ? f->type->fn.ret : ty_mixed;
                return e->type;
            }
            sema_error(&e->u.method.name_tok, "no method '%s' on class %s", mname, c->name);
            e->type = ty_mixed;
            return e->type;
        }
        e->u.method.target = m;
        size_t na = e->u.method.args.len;
        size_t nreq = fn_min_args(m);
        if (na < nreq || na > m->params.len)
            sema_error(&e->u.method.name_tok, "method %s::%s() expects %zu..%zu argument(s), got %zu",
                       c->name, mname, nreq, m->params.len, na);
        for (size_t i = 0; i < na && i < m->params.len; i++) {
            Param *pm = m->params.items[i];
            const Type *pt = resolve_type(sc, pm->type, pm->name_tok);
            Expr *arg = e->u.method.args.items[i];
            const Type *at = check_expr(sc, arg);
            if (pt && !types_compatible(pt, at))
                sema_error(&arg->tok, "argument %zu to %s::%s(): expected %s, got %s",
                           i + 1, c->name, mname, type_to_string(pt), type_to_string(at));
        }
        if (!m->ret) m->ret = fn_has_value_return(m) ? ty_mixed : ty_void;
        e->type = resolve_type(sc, m->ret, m->name_tok);
        return e->type;
    }
    sema_error(&e->u.method.name_tok, "cannot call method '%s' on type %s", mname, type_to_string(ot));
    e->type = ty_mixed;
    return e->type;
}

static const Type *check_builtin_call(Scope *sc, Expr *e, const Builtin *b) {
    size_t na = e->u.call.args.len;
    e->builtin = b->name; /* codegen needs the builtin name too */
    for (size_t i = 0; i < na; i++)
        check_expr(sc, e->u.call.args.items[i]);
    switch (b->shape) {
    case B_STR1:
        if (na != 1) sema_error(&e->tok, "%s() expects 1 argument, got %zu", b->name, na);
        e->type = b->ret;
        break;
    case B_STR2:
        if (na != 2) sema_error(&e->tok, "%s() expects 2 arguments, got %zu", b->name, na);
        e->type = b->ret;
        break;
    case B_INT_STR:
        if (na != 2) sema_error(&e->tok, "%s() expects 2 arguments, got %zu", b->name, na);
        e->type = b->ret;
        break;
    case B_STR_INT:
        if (na < 1) sema_error(&e->tok, "%s() expects at least 1 argument", b->name);
        e->type = b->ret;
        break;
    case B_LEN:
        if (na != 1) sema_error(&e->tok, "%s() expects 1 argument, got %zu", b->name, na);
        e->type = ty_int;
        break;
    case B_CMP:
        if (na != 2) sema_error(&e->tok, "%s() expects 2 arguments, got %zu", b->name, na);
        e->type = ty_int;
        break;
    case B_PRINT:
    case B_MISC:
        e->type = b->ret;
        break;
    case B_ZERO:
        if (na != 0) sema_error(&e->tok, "%s() expects no arguments", b->name);
        e->type = b->ret;
        break;
    case B_ARRAY:
        e->type = type_array_of(ty_mixed);
        break;
    case B_MAXMIN:
        if (na == 0) sema_error(&e->tok, "%s() expects at least 1 argument", b->name);
        e->type = ty_mixed;
        break;
    case B_ABS:
        if (na != 1) sema_error(&e->tok, "%s() expects 1 argument, got %zu", b->name, na);
        e->type = b->ret;
        break;
    case B_INT_INT:
        if (na != 1) sema_error(&e->tok, "%s() expects 1 argument, got %zu", b->name, na);
        e->type = b->ret;
        break;
    case B_JSON: e->type = b->ret; break;
    case B_SPLIT:
        if (na != 2) sema_error(&e->tok, "%s() expects 2 arguments, got %zu", b->name, na);
        e->type = b->ret;
        break;
    case B_JOIN: e->type = ty_string; break;
    case B_KEYS: case B_VALUES: case B_SORTISH: case B_RANGE:
        e->type = b->ret;
        break;
    case B_SORT:
        if (na != 1) sema_error(&e->tok, "%s() expects 1 argument, got %zu", b->name, na);
        e->type = ty_bool;
        break;
    }
    return e->type;
}

static const Type *check_call(Scope *sc, Expr *e) {
    Expr *callee = e->u.call.fn;
    /* method call written as $obj->m(args) already comes as EX_METHOD */
    if (callee->kind == EX_VAR) {
        const char *name = callee->u.var.name;
        /* a local holding a closure/fn value shadows function names — but a
         * bare name (`foo()`) always means the function, never $foo */
        VarSym *local = callee->u.var.bare_name ? NULL : scope_find(sc, name);
        if (!local || (local->type && local->type->kind != TY_FN)) {
            const Builtin *b = builtin_lookup(name);
            if (b) {
                if (b->unsafe && !sc->in_unsafe) {
                    sema_error(&e->tok, "call to unsafe function '%s' requires an unsafe block", name);
                }
                return check_builtin_call(sc, e, b);
            }
            AstFn *fn = find_function(name);
            if (!fn && !local) {
                sema_error(&e->tok, "call to undefined function '%s()'", name);
                for (size_t i = 0; i < e->u.call.args.len; i++)
                    check_expr(sc, e->u.call.args.items[i]);
                e->type = ty_mixed;
                return e->type;
            }
            if (fn) {
                e->u.call.target = fn;
                size_t na = e->u.call.args.len;
                size_t np = fn->params.len;
                size_t nreq = fn_min_args(fn);
                if (na < nreq || na > np)
                    sema_error(&e->tok, "function %s() expects %zu..%zu argument(s), got %zu", name, nreq, np, na);
                for (size_t i = 0; i < na && i < np; i++) {
                    Param *pm = fn->params.items[i];
                    const Type *pt = resolve_type(sc, pm->type, pm->name_tok);
                    Expr *arg = e->u.call.args.items[i];
                    const Type *at = check_expr(sc, arg);
                    if (pt && pm->type && !types_compatible(pt, at))
                        sema_error(&arg->tok, "argument %zu to %s(): expected %s, got %s",
                                   i + 1, name, type_to_string(pt), type_to_string(at));
                }
                /* a function with no written return type is inferred before
                 * the body is ever checked (check_fn_body does it PHP-style:
                 * mixed when it returns values, void otherwise). Calling this
                 * function must not clobber that inferred type. */
                if (!fn->ret_inferred && !fn->ret) {
                    fn->ret = fn_has_value_return(fn) ? ty_mixed : ty_void;
                    fn->ret_inferred = true;
                }
                e->type = fn->ret ? resolve_type(sc, fn->ret, fn->name_tok) : ty_mixed;
                return e->type;
            }
        }
    }
    /* first-class fn value call */
    const Type *ft = check_expr(sc, callee);
    if (ft->kind == TY_FN) {
        size_t na = e->u.call.args.len;
        bool any = ft->fn.variadic;   /* `callable`: arity not pinned */
        if (!any && na != ft->fn.nparams)
            sema_error(&e->tok, "function expects %zu argument(s), got %zu", ft->fn.nparams, na);
        for (size_t i = 0; i < na; i++) {
            const Type *pt = (!any && i < ft->fn.nparams) ? ft->fn.params[i] : NULL;
            Expr *arg = e->u.call.args.items[i];
            const Type *at = check_expr(sc, arg);
            if (pt && !types_compatible(pt, at))
                sema_error(&arg->tok, "argument %zu: expected %s, got %s",
                           i + 1, type_to_string(pt), type_to_string(at));
        }
        e->type = ft->fn.ret ? ft->fn.ret : ty_mixed;
        return e->type;
    }
    /* An hval-typed value may hold a closure at runtime (a property, a map
     * entry, an untyped parameter): allowed like PHP, dispatched by the
     * runtime, which throws a catchable error if it isn't callable. */
    if (ft->kind == TY_MIXED) {
        for (size_t i = 0; i < e->u.call.args.len; i++)
            check_expr(sc, e->u.call.args.items[i]);
        e->type = ty_mixed;
        return e->type;
    }
    for (size_t i = 0; i < e->u.call.args.len; i++)
        check_expr(sc, e->u.call.args.items[i]);
    sema_error(&e->tok, "expression is not callable (%s)", type_to_string(ft));
    e->type = ty_mixed;
    return e->type;
}

static const Type *check_expr(Scope *sc, Expr *e) {
    if (!e) return ty_mixed;
    switch (e->kind) {
    case EX_INT: e->type = ty_int; return e->type;
    case EX_FLOAT: e->type = ty_float; return e->type;
    case EX_STR: e->type = ty_string; return e->type;
    case EX_BOOL: e->type = ty_bool; return e->type;
    case EX_NULL: e->type = ty_mixed; return e->type;
    case EX_TPL: {
        for (size_t i = 0; i < e->u.tpl.exprs.len; i++)
            check_expr(sc, e->u.tpl.exprs.items[i]);
        e->type = ty_string;
        return e->type;
    }    case EX_VAR: {
        const char *name = e->u.var.name;
        /* PHP superglobals: $argc / $argv (read-only, available everywhere) */
        if (name == s_argc || name == s_argv) {
            e->u.var.sym = NULL;
            e->u.var.is_global = true;
            e->type = (name == s_argc) ? (const Type *)ty_int
                                       : (const Type *)type_array_of(ty_string);
            return e->type;
        }
        if (name == s_this && (sc->cur_class || (sc->cur_fn && sc->cur_fn->is_method))) {
            if (!sc->cur_class && sc->cur_fn->cls) sc->cur_class = sc->cur_fn->cls;
            const Type *st = sc->self_type;
            if (!st && sc->cur_class) {
                Type *t = type_new(sc->cur_class->is_interface ? TY_INTERFACE : TY_CLASS);
                t->name = intern(sc->cur_class->name);
                t->decl = sc->cur_class;
                sc->self_type = st = t;
            }
            if (g_clo_depth > 0) {
                /* $this inside a closure: mark the closure (and outer ones) */
                for (int d = g_clo_depth - 1; d >= 0; d--) g_clo_this[d] = true;
            }
            e->u.var.sym = NULL;
            e->type = st ? st : ty_mixed;
            return e->type;
        }
        VarSym *v = scope_find(sc, e->u.var.name);
        if (!v) {
            /* not a local: is it a program-wide `global` variable that some
             * function has declared? Functions are checked before the top
             * level, so a top-level read must still resolve to it. */
            v = program_global_find(e->u.var.name);
        }
        if (!v) {
            sema_error(&e->tok, "undefined variable $%s (assign it a value first)", e->u.var.name);
            e->type = ty_mixed;
            return e->type;
        }
        /* capture analysis: var declared outside the innermost closure? */
        if (g_clo_depth > 0 && !v->is_program_global &&
            v->decl_seq < g_clo_body_seq[g_clo_depth - 1]) {
            note_capture(v);
            for (int d = g_clo_depth - 2; d >= 0; d--) {
                if (v->decl_seq >= g_clo_body_seq[d]) break;
                for (size_t i = 0; i < g_clo_caps[d].len; i++)
                    if (g_clo_caps[d].items[i] == v) goto next_d;
                ptrvec_push(&g_clo_caps[d], v);
            next_d:;
            }
        }
        e->u.var.sym = v;
        if (v->moved_from) {
            sema_error(&e->tok, "use of moved value $%s", e->u.var.name);
        }
        e->type = v->type;
        return e->type;
    }
    case EX_TUPLE_LIT: {
        Type *t = type_new(TY_TUPLE);
        for (size_t i = 0; i < e->u.tuple.elems.len; i++)
            check_expr(sc, e->u.tuple.elems.items[i]);
        e->type = t;
        return e->type;
    }
    case EX_ARRAY_LIT: {
        const Type *elem = NULL;
        bool is_map = e->u.map.vals.len > 0;
        if (is_map) {
            Type *mt = type_new(TY_MAP);
            const Type *kt = NULL, *vt = NULL;
            for (size_t i = 0; i < e->u.map.vals.len; i++) {
                const Type *k2 = check_expr(sc, e->u.map.keys.items[i]);
                const Type *v2 = check_expr(sc, e->u.map.vals.items[i]);
                /* Every entry contributes: a mixed-value map like
                 * ["name" => "Elias", "alter" => 25] must be hval-valued,
                 * otherwise the int entry gets compiled as a string.
                 * Numeric entries are NOT unified either — the runtime keeps
                 * one hval per slot, so declaring [1, 2.5] as float would
                 * reinterpret the int slot's bits as a double. Any difference
                 * means "mixed", and reads then convert properly. */
                if (!kt) kt = k2;
                else if (kt != k2) kt = ty_mixed;
                if (!vt) vt = v2;
                else if (vt != v2) vt = ty_mixed;
            }
            mt->key = kt ? kt : ty_mixed;
            mt->val = vt ? vt : ty_mixed;
            e->type = mt;
            return e->type;
        }
        for (size_t i = 0; i < e->u.arr.elems.len; i++) {
            const Type *et = check_expr(sc, e->u.arr.elems.items[i]);
            if (!elem) elem = et;
            /* see the map case: slots hold hvals, so never unify numerics
             * here — [1, 2.5] must stay mixed, not become float */
            else if (elem != et) elem = ty_mixed;
        }
        Type *at = type_new(TY_ARRAY);
        at->elem = elem ? elem : ty_mixed;
        e->type = at;
        return e->type;
    }
    case EX_INDEX: {
        const Type *ot = check_expr(sc, e->u.index.obj);
        const Type *it = e->u.index.idx ? check_expr(sc, e->u.index.idx) : ty_int;
        if (ot->kind == TY_ARRAY || ot->kind == TY_VEC || ot->kind == TY_MAP || ot->kind == TY_SET) {
            const Type *want = ot->kind == TY_MAP ? ot->key : ty_int;
            /* PHP-style: a string key promotes a plain array to a map */
            if (ot->kind == TY_ARRAY && it && it->kind == TY_STRING) {
                e->type = ty_mixed;
                return e->type;
            }
            if (it && want && !types_compatible(want, it) &&
                want->kind != TY_MIXED && it->kind != TY_MIXED)
                sema_error(&e->tok, "index must be %s, got %s", type_to_string(want), type_to_string(it));
            e->type = elem_type_of(ot, it);
            if (!e->type) e->type = ty_mixed;
            return e->type;
        }
        if (ot->kind == TY_STRING) { e->type = ty_string; return e->type; }
        /* PHP-style: indexing a mixed value (e.g. a map from a foreach over
         * an untyped array) is allowed and yields mixed. */
        if (ot->kind == TY_MIXED) { e->type = ty_mixed; return e->type; }
        sema_error(&e->tok, "cannot index value of type %s", type_to_string(ot));
        e->type = ty_mixed;
        return e->type;
    }
    case EX_FIELD: {
        const Type *ot = check_expr(sc, e->u.field.obj);
        if (ot->kind == TY_OWN || ot->kind == TY_RC || ot->kind == TY_REF || ot->kind == TY_MUTREF)
            ot = ot->elem;
        if (ot->kind == TY_CLASS || ot->kind == TY_STRUCT) {
            AstClass *c = ot->decl;
            if (!c) c = find_class(ot->name);
            if (c) {
                StructField *f = class_find_field(c, e->u.field.name);
                if (!f && c->base) f = class_find_field(c->base, e->u.field.name);
                if (f) {
                    e->u.field.sf = f;
                    e->u.field.vcls = c;      /* static type of the receiver */
                    e->u.field.owner = ot;
                    /* an untyped property is hval-valued, never NULL-typed:
                     * `pub $cb = null;` is the PHP-style dynamic member */
                    e->type = f->type ? resolve_type(sc, f->type, f->name_tok) : ty_mixed;
                    return e->type;
                }
            }
            sema_error(&e->tok, "no field '%s' on %s", e->u.field.name, type_to_string(ot));
            e->type = ty_mixed;
            return e->type;
        }
        if (e->u.field.is_static) {
            /* Cls::CONST or Enum::CASE */
            AstEnum *en = find_enum(e->u.field.obj->kind == EX_VAR ? e->u.field.obj->u.var.name : e->u.field.name);
            if (en && e->u.field.obj->kind == EX_VAR) {
                AstEnum *en2 = find_enum(e->u.field.obj->u.var.name);
                if (en2) {
                    for (size_t i = 0; i < en2->cases.len; i++) {
                        EnumCase *ec = en2->cases.items[i];
                        if (ec->name == intern(e->u.field.name)) {
                            e->type = sym_type_of_enum(en2);
                            e->u.field.case_index = (int)ec->index;
                            return e->type;
                        }
                    }
                    sema_error(&e->tok, "enum %s has no case '%s'", en2->name, e->u.field.name);
                    e->type = ty_mixed;
                    return e->type;
                }
            }
            AstClass *c = find_class(e->u.field.obj->kind == EX_VAR ? e->u.field.obj->u.var.name : "?");
            if (c) {
                StructField *f = class_find_field(c, e->u.field.name);
                if (f) {
                    e->u.field.sf = f;
                    e->type = resolve_type(sc, f->type, f->name_tok);
                    return e->type;
                }
            }
        }
        sema_error(&e->tok, "no member '%s' on type %s", e->u.field.name, type_to_string(ot));
        e->type = ty_mixed;
        return e->type;
    }
    case EX_CALL: return check_call(sc, e);
    case EX_METHOD: return check_method_call(sc, e);
    case EX_STATIC: {
        const Builtin *b = builtin_lookup(e->u.staticcall.name);
        AstFn *fn = find_function(e->u.staticcall.name);
        AstClass *cls = find_class(e->u.staticcall.cls);
        if (cls) {
            AstFn *m = class_find_method(cls, e->u.staticcall.name);
            if (!m && cls->base) m = class_find_method(cls->base, e->u.staticcall.name);
            if (m) {
                e->u.staticcall.target = m;
                size_t na = e->u.staticcall.args.len;
                size_t nreq = fn_min_args(m);
                if (na < nreq || na > m->params.len)
                    sema_error(&e->tok, "%s::%s() expects %zu..%zu argument(s), got %zu",
                               cls->name, m->name, nreq, m->params.len, na);
                for (size_t i = 0; i < na && i < m->params.len; i++) {
                    Param *pm = m->params.items[i];
                    const Type *pt = resolve_type(sc, pm->type, pm->name_tok);
                    Expr *arg = e->u.staticcall.args.items[i];
                    const Type *at = check_expr(sc, arg);
                    if (pt && pm->type && !types_compatible(pt, at))
                        sema_error(&arg->tok, "argument %zu: expected %s, got %s",
                                   i + 1, type_to_string(pt), type_to_string(at));
                }
                if (!m->ret) m->ret = fn_has_value_return(m) ? ty_mixed : ty_void;
                e->type = resolve_type(sc, m->ret, m->name_tok);
                return e->type;
            }
        }
        if (b) {
            Expr tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.kind = EX_CALL;
            tmp.tok = e->tok;
            tmp.u.call.fn = expr_new(EX_VAR, e->tok);
            tmp.u.call.fn->u.var.name = e->u.staticcall.name;
            tmp.u.call.args = e->u.staticcall.args;
            const Type *t = check_builtin_call(sc, &tmp, b);
            e->builtin = e->u.staticcall.name;
            e->type = t;
            return e->type;
        }
        if (fn) {
            size_t na = e->u.staticcall.args.len;
            size_t nreq = fn_min_args(fn);
            if (na < nreq || na > fn->params.len)
                sema_error(&e->tok, "%s() expects %zu..%zu argument(s), got %zu", fn->name, nreq, fn->params.len, na);
            for (size_t i = 0; i < na && i < fn->params.len; i++) {
                Param *pm = fn->params.items[i];
                const Type *pt = resolve_type(sc, pm->type, pm->name_tok);
                Expr *arg = e->u.staticcall.args.items[i];
                const Type *at = check_expr(sc, arg);
                if (pt && pm->type && !types_compatible(pt, at))
                    sema_error(&arg->tok, "argument %zu: expected %s, got %s",
                               i + 1, type_to_string(pt), type_to_string(at));
            }
            if (!fn->ret) fn->ret = fn_has_value_return(fn) ? ty_mixed : ty_void;
            e->type = resolve_type(sc, fn->ret, fn->name_tok);
            return e->type;
        }
        sema_error(&e->tok, "unknown function or class '%s'", e->u.staticcall.cls);
        e->type = ty_mixed;
        return e->type;
    }
    case EX_NEW: {
        AstClass *c = find_class(e->u.newexpr.cls);
        if (!c && strcmp(e->u.newexpr.cls, "Exception") == 0) {
            /* builtin Exception class: 0..1 args, throws carry a message */
            size_t na = e->u.newexpr.args.len;
            if (na > 1)
                sema_error(&e->tok, "Exception::__construct() expects at most 1 argument");
            for (size_t i = 0; i < na; i++) check_expr(sc, e->u.newexpr.args.items[i]);
            Type *t = type_new(TY_CLASS);
            t->name = intern("Exception");
            e->type = t;
            return e->type;
        }
        if (!c) {
            sema_error(&e->tok, "unknown class '%s'", e->u.newexpr.cls);
            e->type = ty_mixed;
            return e->type;
        }
        e->u.newexpr.cls_res = c;
        AstFn *ctor = class_find_method(c, "__construct");
        if (!ctor && c->base) ctor = class_find_method(c->base, "__construct");
        size_t na = e->u.newexpr.args.len;
        if (ctor) {
            size_t np = ctor->params.len;
            size_t nreq = fn_min_args(ctor);
            if (na < nreq || na > np)
                sema_error(&e->tok, "%s::__construct() expects %zu..%zu argument(s), got %zu",
                           c->name, nreq, np, na);
            for (size_t i = 0; i < na && i < np; i++) {
                Param *pm = ctor->params.items[i];
                const Type *pt = resolve_type(sc, pm->type, pm->name_tok);
                Expr *arg = e->u.newexpr.args.items[i];
                const Type *at = check_expr(sc, arg);
                if (pt && pm->type && !types_compatible(pt, at))
                    sema_error(&arg->tok, "argument %zu to %s(): expected %s, got %s",
                               i + 1, c->name, type_to_string(pt), type_to_string(at));
            }
        } else if (na != 0) {
            sema_error(&e->tok, "%s has no constructor, expected 0 arguments, got %zu", c->name, na);
        }
        /* new returns own<T> */
        Type *t = type_new(TY_OWN);
        t->elem = class_type_of(c);
        e->type = t;
        return e->type;
    }
    case EX_BIN: return check_bin(sc, e);
    case EX_UN: {
        const Type *t = check_expr(sc, e->u.un.operand);
        switch (e->u.un.op.kind) {
        case T_NOT: e->type = ty_bool; break;
        case T_MINUS: case T_PLUS:
            if (!type_is_numeric(t->kind))
                sema_error(&e->u.un.op, "unary '%s' requires a numeric operand, got %s",
                           tok_kind_name(e->u.un.op.kind), type_to_string(t));
            e->type = t;
            break;
        case T_TILDE:
            if (!type_is_integral(t->kind))
                sema_error(&e->u.un.op, "unary '~' requires an integer operand, got %s", type_to_string(t));
            e->type = t;
            break;
        default: e->type = t; break;
        }
        return e->type;
    }
    case EX_ASSIGN: {
        Expr *tgt = e->u.assign.target;
        const Type *vt = e->u.assign.value ? check_expr(sc, e->u.assign.value) : ty_mixed;
        /* determine target type & mutability */
        const Type *tt = NULL;
        if (tgt->kind == EX_VAR) {
            VarSym *v = scope_find(sc, tgt->u.var.name);
            if (!v && sc->cur_fn == NULL)
                v = program_global_find(tgt->u.var.name);   /* `global $x;` seen earlier */
            if (!v) {
                /* PHP-style: assignment declares the variable. Optional
                 * type annotation: "$x: int = 5". */
                const Type *ann = e->u.assign.ann
                    ? resolve_type(sc, e->u.assign.ann, tgt->tok) : NULL;
                if (ann && e->u.assign.value &&
                    !types_compatible(ann, vt) && ann->kind != TY_MIXED)
                    sema_error(&e->tok, "cannot initialize $%s (type %s) with %s",
                               tgt->u.var.name, type_to_string(ann), type_to_string(vt));
                v = scope_declare(sc, tgt->u.var.name, ann ? ann : vt, tgt->tok, true);
                v->is_auto = true;
                /* register so later `global $x;` in functions binds THIS sym
                 * (is_program_global is set by the ST_GLOBAL binder itself) */
                if (sc->cur_fn == NULL && !program_global_find(tgt->u.var.name))
                    ptrvec_push(&g_program_globals, v);
                tgt->u.var.sym = v;
            } else {
                tgt->u.var.sym = v; /* codegen needs the symbol for naming */
                tt = v->type;
                if (!v->is_mut)
                    sema_error(&tgt->tok, "cannot assign to immutable variable $%s (declare with let mut)", tgt->u.var.name);
                if (v->type && (v->type->kind == TY_OWN || v->type->kind == TY_RC) &&
                    e->u.assign.value) {
                    /* move semantics */
                    VarSym *src = e->u.assign.value->kind == EX_VAR ? scope_find(sc, e->u.assign.value->u.var.name) : NULL;
                    if (src && (src->type->kind == TY_OWN || src->type->kind == TY_RC)) {
                        src->moved_from = true;
                    }
                }
            }
        } else if (tgt->kind == EX_INDEX || tgt->kind == EX_FIELD) {
            tt = check_expr(sc, tgt);
            /* element assignment on collections is allowed by runtime;
             * PHP-style: elements accept any value (arrays are dynamic) */
            if (tgt->kind == EX_INDEX) tt = NULL; /* skip element-type check */
        } else {
            tt = check_expr(sc, tgt);
            sema_error(&tgt->tok, "invalid assignment target");
        }
        /* compound assignment: a .= b, a += b ... */
        TokKind op = e->u.assign.op.kind;
        if (op != T_ASSIGN) {
            if (op == T_DOTASSIGN) {
                if (vt->kind != TY_STRING || (tt && tt->kind != TY_STRING && tt->kind != TY_MIXED))
                    sema_error(&e->u.assign.op, ".= requires string operands");
            } else if (type_is_numeric(tt ? tt->kind : TY_ERROR) && type_is_numeric(vt->kind)) {
                /* ok */
            } else if ((op == T_SHLASSIGN || op == T_SHRASSIGN) &&
                       (vt->kind == TY_STRING || vt->kind == TY_MIXED || type_is_numeric(vt->kind)) &&
                       (!tt || tt->kind == TY_MIXED || tt->kind == TY_STRING ||
                        type_is_numeric(tt->kind))) {
                /* shifts accept numeric strings and hval operands */
            } else if (op != T_DOTASSIGN &&
                       ((tt && tt->kind == TY_MIXED) || vt->kind == TY_MIXED)) {
                /* PHP-style: mixed operands (e.g. map element of untyped
                 * map) flow through the generic runtime arithmetic. */
            } else {
                sema_error(&e->u.assign.op, "invalid operand types for compound assignment");
            }
        }
        if (tt && !types_compatible(tt, vt) && tt->kind != TY_MIXED && vt->kind != TY_MIXED)
            sema_error(&e->tok, "cannot assign %s to %s", type_to_string(vt), type_to_string(tt));
        e->type = tt ? tt : ty_mixed;
        return e->type;
    }
    case EX_TERNARY: {
        const Type *ct = check_expr(sc, e->u.tern.cond);
        (void)ct;
        const Type *a = check_expr(sc, e->u.tern.then);
        const Type *b = check_expr(sc, e->u.tern.els);
        e->type = types_compatible(a, b) ? a : (types_compatible(b, a) ? b : ty_mixed);
        return e->type;
    }
    case EX_MATCH: {
        const Type *st = check_expr(sc, e->u.matchexpr.subject);
        (void)st;
        const Type *rt = NULL;
        for (size_t i = 0; i < e->u.matchexpr.cases.len; i++) {
            MatchCase *mc = e->u.matchexpr.cases.items[i];
            for (size_t k = 0; k < mc->patterns.len; k++)
                check_expr(sc, mc->patterns.items[k]);
            const Type *bt = check_expr(sc, mc->body);
            if (!rt) rt = bt;
            else if (!types_compatible(rt, bt)) rt = ty_mixed;
        }
        if (e->u.matchexpr.dflt) {
            const Type *bt = check_expr(sc, e->u.matchexpr.dflt);
            if (!rt) rt = bt;
        } else if (e->u.matchexpr.cases.len == 0) {
            sema_error(&e->tok, "match expression has no arms");
        }
        e->type = rt ? rt : ty_mixed;
        return e->type;
    }
    case EX_CLOSURE: {
        AstFn *fn = e->u.closure.fn;
        fn->is_closure = true;
        if (g_clo_depth >= 32) {
            sema_error(&e->tok, "closures nested too deeply");
            e->type = ty_mixed;
            return e->type;
        }
        Type *ft = type_new(TY_FN);
        ft->fn.nparams = fn->params.len;
        ft->fn.params = xmalloc(sizeof(Type *) * (fn->params.len ? fn->params.len : 1));
        /* infer param types from context: untyped closure params resolve to
           the expected signature's types when a target is known */
        for (size_t i = 0; i < fn->params.len; i++) {
            Param *pm = fn->params.items[i];
            const Type *pt = pm->type ? resolve_type(sc, pm->type, pm->name_tok) : ty_mixed;
            ((const Type **)ft->fn.params)[i] = pt;
            pm->resolved = pt;
        }
        /* push closure context; body scope seq marks the capture boundary */
        Scope fs;
        int d = g_clo_depth++;
        g_clo_fn[d] = fn;
        g_clo_caps[d] = (PtrVec){0};
        g_clo_this[d] = false;
        scope_init(&fs, sc);
        fs.cur_fn = fn;
        fs.cur_class = sc->cur_class;
        fs.self_type = sc->self_type;
        fs.in_unsafe = sc->in_unsafe;
        g_clo_body_seq[d] = fs.seq;
        /* declare real params */
        for (size_t i = 0; i < fn->params.len; i++) {
            Param *pm = fn->params.items[i];
            VarSym *v = scope_declare(&fs, pm->name, pm->resolved, pm->name_tok, pm->is_mut);
            pm->sym = v;
            v->is_param = true;
        }
        /* declare use() captures: by-value copies into env hvals; by-ref
         * captures (&$x) alias the outer variable through a shared heap
         * box so writes inside the closure are visible outside. */
        for (size_t i = 0; i < fn->use_vars.len; i++) {
            Param *pm = fn->use_vars.items[i];
            VarSym *outer = scope_find(sc, pm->name);
            if (!outer) {
                sema_error(&pm->name_tok, "use(): undefined variable $%s", pm->name);
                continue;
            }
            if (pm->by_ref) {
                outer->captured_byref = true;
                outer->type = type_new(TY_MIXED);   /* boxed as hval */
            }
            if (outer->is_global_alias || outer->is_program_global) {
                /* `global $x;` + `use ($x)`: skip capture; the closure body
                 * references the file-scope storage directly. */
                if (fs.nvars < MAX_SCOPE) fs.vars[fs.nvars++] = outer;
                continue;
            }
            if (fs.nvars < MAX_SCOPE) fs.vars[fs.nvars++] = outer;
            note_capture(outer);
        }
        /* closures without an explicit return type infer it from their
         * first `return expr;` — like PHP's ReturnTypeWillChange, simple */
        if (!fn->ret) fn->ret = type_new(TY_MIXED);
        for (size_t i = 0; i < fn->body.len; i++)
            check_stmt(&fs, fn->body.items[i]);
        fn->captures = g_clo_caps[d];
        fn->uses_this = g_clo_this[d];
        const Type *ret = fn->ret ? resolve_type(sc, fn->ret, fn->name_tok) : ty_void;
        ft->fn.ret = ret;
        fn->ftype = ft;
        e->type = ft;
        g_clo_depth--;
        return e->type;
    }
    case EX_BORROW: case EX_MUTBORROW: {
        Expr *op = e->u.borrow.operand;
        if (op->kind != EX_VAR) {
            sema_error(&e->tok, "can only borrow variables (found %s expression)", expr_kind_name(op->kind));
            e->type = ty_mixed;
            return e->type;
        }
        VarSym *v = scope_find(sc, op->u.var.name);
        if (!v) {
            sema_error(&op->tok, "undefined variable $%s", op->u.var.name);
            e->type = ty_mixed;
            return e->type;
        }
        if (g_clo_depth > 0 && v->decl_seq < g_clo_body_seq[g_clo_depth - 1])
            note_capture(v);
        bool is_mut = e->kind == EX_MUTBORROW;
        if (is_mut && !v->is_mut)
            sema_error(&e->tok, "cannot mutably borrow immutable $%s", v->name);
        /* borrow checker rule: many readers XOR one writer */
        int existing = loans_of(v);
        if (is_mut && existing)
            sema_error(&e->tok, "cannot borrow $%s as mutable because it is also borrowed as immutable",
                       v->name);
        if (!is_mut && (existing & 2))
            sema_error(&e->tok, "cannot borrow $%s as immutable because it is also borrowed as mutable",
                       v->name);
        loans_push(v, is_mut, e->tok.line);
        op->u.var.sym = v; /* codegen needs the symbol to name the C var */
        Type *t = type_new(is_mut ? TY_MUTREF : TY_REF);
        t->elem = v->type;
        e->type = t;
        return e->type;
    }
    case EX_DEREF: {
        const Type *t = check_expr(sc, e->u.deref.operand);
        if (!type_is_pointerish(t->kind)) {
            sema_error(&e->tok, "cannot dereference non-pointer type %s", type_to_string(t));
            e->type = ty_mixed;
            return e->type;
        }
        if (t->kind == TY_PTR && !sc->in_unsafe)
            sema_error(&e->tok, "dereferencing a raw pointer requires an unsafe block");
        e->type = t->elem ? t->elem : ty_mixed;
        return e->type;
    }
    case EX_CAST: {
        const Type *from = check_expr(sc, e->u.cast.operand);
        const Type *to = resolve_type(sc, (Type *)e->u.cast.to, e->u.cast.ty_tok);
        if (to->kind == TY_PTR && !sc->in_unsafe)
            sema_error(&e->tok, "cast to raw pointer '%s' requires an unsafe block", type_to_string(to));
        if (to->kind == TY_PTR && !type_is_pointerish(from->kind) && from->kind != TY_INT)
            sema_error(&e->tok, "cannot cast %s to raw pointer", type_to_string(from));
        e->type = to;
        return e->type;
    }
    case EX_IS: {
        check_expr(sc, e->u.ischeck.operand);
        resolve_type(sc, (Type *)e->u.ischeck.to, e->u.ischeck.ty_tok);
        e->type = ty_bool;
        return e->type;
    }
    }
    e->type = ty_mixed;
    return e->type;
}

const char *expr_kind_name(ExprKind k) {
    switch (k) {
    case EX_INT: return "int";
    case EX_FLOAT: return "float";
    case EX_STR: return "string";
    case EX_TPL: return "string interpolation";
    case EX_BOOL: return "bool";
    case EX_NULL: return "null";
    case EX_VAR: return "variable";
    case EX_INDEX: return "index";
    case EX_FIELD: return "field access";
    case EX_CALL: return "function call";
    case EX_METHOD: return "method call";
    case EX_STATIC: return "static call";
    case EX_NEW: return "new";
    case EX_BIN: return "binary";
    case EX_UN: return "unary";
    case EX_ASSIGN: return "assignment";
    case EX_TERNARY: return "ternary";
    case EX_MATCH: return "match";
    case EX_CLOSURE: return "closure";
    case EX_ARRAY_LIT: return "array literal";
    case EX_MAP_LIT: return "map literal";
    case EX_TUPLE_LIT: return "tuple";
    case EX_BORROW: return "borrow";
    case EX_MUTBORROW: return "mutable borrow";
    case EX_DEREF: return "dereference";
    case EX_CAST: return "cast";
    case EX_IS: return "is";
    default: return "expression";
    }
}

/* ---------------- statements ---------------- */
static void check_block(Scope *sc, PtrVec stmts);

static void check_stmt(Scope *sc, Stmt *s) {
    switch (s->kind) {
    case ST_EXPR:
        check_expr(sc, s->u.expr.expr);
        break;
    case ST_GLOBAL: {
        /* PHP `global $a, $b;` — bind each name to the program-global symbol
         * (one shared VarSym per name across the whole program). */
        for (size_t i = 0; i < s->u.globals.names.len; i++) {
            const char *name = intern(s->u.globals.names.items[i]);
            Token tok = *(Token *)s->u.globals.toks.items[i];
            /* prefer an existing top-level declaration ($x = ... at script
             * scope) so both bind to one C variable */
            Scope *gsc = sc;
            while (gsc->parent) gsc = gsc->parent;
            VarSym *gv = NULL;
            for (size_t j = 0; j < gsc->nvars; j++) {
                if (gsc->vars[j]->name == name && !gsc->vars[j]->is_global_alias) {
                    gv = gsc->vars[j];
                    break;
                }
            }
            if (!gv) gv = program_global_find(name);
            if (!gv) gv = program_global_get(name, tok);
            else if (!program_global_find(name)) ptrvec_push(&g_program_globals, gv);
            gv->is_mut = true;
            gv->is_auto = true;
            gv->is_program_global = true;   /* storage becomes file-scope */
            /* expose the SAME symbol under this local scope so all uses in
             * this function share one uid => one C variable */
            VarSym *lv = xcalloc(1, sizeof(VarSym));
            *lv = *gv;                       /* same name/type/uid */
            lv->is_global_alias = true;
            lv->is_mut = true;
            if (sc->nvars < MAX_SCOPE) sc->vars[sc->nvars++] = lv;
            ptrvec_push(&s->u.globals.syms, gv);   /* codegen: file-scope storage */
        }
        break;
    }
    case ST_LET: {
        const Type *it = check_expr(sc, s->u.let.init);
        const Type *t = NULL;
        if (s->u.let.ann) {
            t = resolve_type(sc, s->u.let.ann, s->tok);
            if (!types_compatible(t, it) && t->kind != TY_MIXED)
                sema_error(&s->tok, "initial value for $%s has type %s, expected %s",
                           s->u.let.name, type_to_string(it), type_to_string(t));
        } else {
            t = it;
        }
        VarSym *v = scope_declare(sc, s->u.let.name, t, s->u.let.name_tok, s->u.let.is_mut);
        s->u.let.sym = v;
        /* move semantics for own<Rc> */
        if (it && (it->kind == TY_OWN || it->kind == TY_RC) &&
            s->u.let.init->kind == EX_VAR) {
            VarSym *src = scope_find(sc, s->u.let.init->u.var.name);
            if (src && !s->u.let.is_mut) {
                /* moving out of a non-mut binding is fine (copy for Rc, move for own) */
                src->moved_from = true;
            }
        }
        s->u.let.init->is_stmt_value = false;
        (void)v;
        break;
    }
    case ST_RETURN: {
        const Type *rt = sc->cur_fn && sc->cur_fn->ret ? resolve_type(sc, sc->cur_fn->ret, s->tok) : ty_void;
        if (s->u.ret.value) {
            const Type *vt = check_expr(sc, s->u.ret.value);
            /* an hval-typed value (untyped property, mixed call, map entry)
             * converts at runtime — PHP-style dynamic returns are allowed */
            bool boxed = vt && (vt->kind == TY_MIXED || vt->kind == TY_OPTION ||
                                vt->kind == TY_RESULT);
            if (rt && rt->kind != TY_VOID && rt->kind != TY_MIXED && !boxed &&
                !types_compatible(rt, vt))
                sema_error(&s->tok, "return type mismatch: expected %s, got %s",
                           type_to_string(rt), type_to_string(vt));
            if (rt && rt->kind == TY_VOID)
                sema_error(&s->tok, "returning a value from a void function");
        } else if (rt && rt->kind != TY_VOID && rt->kind != TY_MIXED) {
            sema_error(&s->tok, "non-void function must return a value of %s", type_to_string(rt));
        }
        break;
    }
    case ST_ECHO:
        for (size_t i = 0; i < s->u.echo.exprs.len; i++)
            check_expr(sc, s->u.echo.exprs.items[i]);
        break;
    case ST_PRINT:
        check_expr(sc, s->u.print.expr);
        break;
    case ST_IF: {
        const Type *ct = check_expr(sc, s->u.if_.cond);
        if (ct->kind != TY_BOOL && ct->kind != TY_MIXED)
            sema_error(&s->tok, "if condition must be bool, got %s", type_to_string(ct));
        check_block(sc, s->u.if_.then);
        check_block(sc, s->u.if_.els);
        break;
    }
    case ST_WHILE: {
        const Type *ct = check_expr(sc, s->u.while_.cond);
        if (ct->kind != TY_BOOL && ct->kind != TY_MIXED)
            sema_error(&s->tok, "while condition must be bool, got %s", type_to_string(ct));
        sc->loop_depth++;
        check_block(sc, s->u.while_.body);
        sc->loop_depth--;
        break;
    }
    case ST_DO: {
        sc->loop_depth++;
        check_block(sc, s->u.do_.body);
        sc->loop_depth--;
        const Type *ct = check_expr(sc, s->u.do_.cond);
        if (ct->kind != TY_BOOL && ct->kind != TY_MIXED)
            sema_error(&s->tok, "do-while condition must be bool, got %s", type_to_string(ct));
        break;
    }
    case ST_SWITCH: {
        const Type *ct = check_expr(sc, s->u.switch_.cond);
        bool cond_num = ct->kind == TY_INT || ct->kind == TY_I8 ||
                        ct->kind == TY_I16 || ct->kind == TY_I32 ||
                        ct->kind == TY_I64 || ct->kind == TY_FLOAT ||
                        ct->kind == TY_F32 || ct->kind == TY_F64;
        bool cond_str = ct->kind == TY_STRING || ct->kind == TY_MIXED;
        if (!cond_num && !cond_str)
            sema_error(&s->tok, "switch condition must be int or string, got %s",
                       type_to_string(ct));
        sc->loop_depth++;  /* 'break' exits a switch, like PHP */
        for (size_t i = 0; i < s->u.switch_.cases.len; i++) {
            SwitchCase *cse = s->u.switch_.cases.items[i];
            for (size_t k = 0; k < cse->patterns.len; k++) {
                Expr *pat = cse->patterns.items[k];
                const Type *pt = check_expr(sc, pat);
                bool ok = (cond_num && (pt->kind == TY_INT || pt->kind == TY_I8 ||
                                        pt->kind == TY_I16 || pt->kind == TY_I32 ||
                                        pt->kind == TY_I64 || pt->kind == TY_FLOAT ||
                                        pt->kind == TY_F32 || pt->kind == TY_F64 ||
                                        pt->kind == TY_MIXED)) ||
                          (cond_str && (pt->kind == TY_STRING || pt->kind == TY_MIXED));
                if (!ok)
                    sema_error(&pat->tok, "case type %s does not match switch type %s",
                               type_to_string(pt), type_to_string(ct));
            }
            check_block(sc, cse->body);
        }
        sc->loop_depth--;
        break;
    }
    case ST_FOR: {
        for (size_t i = 0; i < s->u.for_.init.len; i++)
            check_stmt(sc, s->u.for_.init.items[i]);
        if (s->u.for_.cond) {
            const Type *ct = check_expr(sc, s->u.for_.cond);
            if (ct->kind != TY_BOOL && ct->kind != TY_MIXED)
                sema_error(&s->tok, "for condition must be bool, got %s", type_to_string(ct));
        }
        for (size_t i = 0; i < s->u.for_.step.len; i++)
            check_stmt(sc, s->u.for_.step.items[i]);
        sc->loop_depth++;
        check_block(sc, s->u.for_.body);
        sc->loop_depth--;
        break;
    }
    case ST_FOREACH: {
        const Type *it = check_expr(sc, s->u.foreach.iter);
        const Type *elem = NULL, *key = NULL;
        if (it->kind == TY_ARRAY || it->kind == TY_VEC || it->kind == TY_SET) {
            elem = it->elem;
            key = ty_int;
        } else if (it->kind == TY_MAP) {
            elem = it->val;
            key = it->key;
        } else if (it->kind == TY_STRING) {
            elem = ty_string;
            key = ty_int;
        } else if (it->kind == TY_MIXED || it->kind == TY_OPTION ||
                   it->kind == TY_RESULT) {
            /* dynamically typed container: the runtime iterator decides */
            elem = ty_mixed;
            key = ty_mixed;
        } else {
            sema_error(&s->tok, "foreach expects an iterable, got %s", type_to_string(it));
            elem = ty_mixed;
        }
        Scope inner;
        scope_init(&inner, sc);
        inner.cur_fn = sc->cur_fn;
        inner.cur_class = sc->cur_class;
        inner.in_unsafe = sc->in_unsafe;
        inner.loop_depth = sc->loop_depth + 1;
        inner.self_type = sc->self_type;
        scope_declare(&inner, s->u.foreach.var, elem, s->u.foreach.var_tok, false);
        if (s->u.foreach.kvar) scope_declare(&inner, s->u.foreach.kvar, key, s->u.foreach.var_tok, false);
        s->u.foreach.vsym = inner.vars[inner.nvars - (s->u.foreach.kvar ? 2 : 1)];
        if (s->u.foreach.kvar) s->u.foreach.ksym = inner.vars[inner.nvars - 1];
        check_block(&inner, s->u.foreach.body);
        break;
    }
    case ST_BREAK: case ST_CONTINUE:
        if (sc->loop_depth == 0)
            sema_error(&s->tok, "'%s' outside of a loop", s->kind == ST_BREAK ? "break" : "continue");
        break;
    case ST_BLOCK: {
        Scope inner;
        scope_init(&inner, sc);
        inner.cur_fn = sc->cur_fn;
        inner.cur_class = sc->cur_class;
        inner.in_unsafe = sc->in_unsafe;
        inner.loop_depth = sc->loop_depth;
        inner.self_type = sc->self_type;
        check_block(&inner, s->u.block.stmts);
        break;
    }
    case ST_UNSAFE_BLOCK: {
        Scope inner;
        scope_init(&inner, sc);
        inner.cur_fn = sc->cur_fn;
        inner.cur_class = sc->cur_class;
        inner.in_unsafe = true;
        inner.loop_depth = sc->loop_depth;
        inner.self_type = sc->self_type;
        check_block(&inner, s->u.unsafe.stmts);
        break;
    }
    case ST_TRY: {
        check_block(sc, s->u.try.body);
        for (size_t i = 0; i < s->u.try.catches.len; i++) {
            CatchClause *cc = s->u.try.catches.items[i];
            Scope inner;
            scope_init(&inner, sc);
            inner.cur_fn = sc->cur_fn;
            inner.cur_class = sc->cur_class;
            inner.in_unsafe = sc->in_unsafe;
            inner.loop_depth = sc->loop_depth;
            inner.self_type = sc->self_type;
            const Type *et = cc->type ? resolve_type(sc, (Type *)cc->type, cc->var_tok) : ty_mixed;
            VarSym *cv = scope_declare(&inner, cc->var, et, cc->var_tok, false);
            cc->sym = cv;
            check_block(&inner, cc->body);
        }
        if (s->u.try.fin.len) check_block(sc, s->u.try.fin);
        break;
    }
    case ST_THROW: {
        const Type *t = check_expr(sc, s->u.throw.expr);
        if (t->kind != TY_STRING && t->kind != TY_MIXED &&
            t->kind != TY_CLASS && t->kind != TY_OWN)
            sema_error(&s->tok, "can only throw strings or exceptions, got %s", type_to_string(t));
        break;
    }
    }
}

static void check_block(Scope *sc, PtrVec stmts) {
    for (size_t i = 0; i < stmts.len; i++)
        check_stmt(sc, stmts.items[i]);
    loans_release_all();
}

/* ---------------- function & class bodies ---------------- */
/* does this function contain any `return <value>;`? (stmt_chain_returns
 * already walks nested blocks, loops and ifs) */
static bool fn_has_value_return(AstFn *fn) {
    for (size_t i = 0; i < fn->body.len; i++)
        if (stmt_chain_returns(fn->body.items[i])) return true;
    return false;
}

static void check_fn_body(Scope *parent, AstFn *fn, const Type *self_type) {
    Scope sc;
    scope_init(&sc, parent);
    sc.cur_fn = fn;
    sc.self_type = self_type;
    for (size_t i = 0; i < fn->params.len; i++) {
        Param *pm = fn->params.items[i];
        const Type *pt = pm->type ? resolve_type(&sc, pm->type, pm->name_tok) : ty_mixed;
        pm->resolved = pt;
        VarSym *v = scope_declare(&sc, pm->name, pt, pm->name_tok, pm->is_mut);
        v->is_param = true;
        pm->sym = v;
    }
    /* PHP-style: a function with no written return type is checked as if it
     * returned mixed when any statement returns a value (its type is then
     * inferred before any call site is checked). Only a fn with no value
     * returns at all behaves as void. */
    if (fn && !fn->ret && fn_has_value_return(fn))
        fn->ret = ty_mixed;
    for (size_t i = 0; i < fn->body.len; i++)
        check_stmt(&sc, fn->body.items[i]);
    if (fn->ret && fn->ret->kind != TY_VOID && fn->body.len) {
        /* not a full flow analysis; simple check: last stmt return? */
        Stmt *last = fn->body.items[fn->body.len - 1];
        if (last->kind != ST_RETURN && fn->ret->kind != TY_MIXED) {
            /* warn only, codegen inserts return 0 */
        }
    }
}

static void check_class(AstClass *c) {
    Scope sc;
    scope_init(&sc, NULL);
    sc.cur_class = c;
    sc.self_type = class_type_of(c);
    /* field types — an untyped field's default also fixes its type, so
     * `pub $x = 1;` is an int property and `pub $cb = null;` is hval */
    for (size_t i = 0; i < c->fields.len; i++) {
        StructField *f = c->fields.items[i];
        if (f->type) {
            resolve_type(&sc, f->type, f->name_tok);
        } else if (f->dflt) {
            const Type *dt = check_expr(&sc, f->dflt);
            f->type = dt ? (Type *)dt : type_new(TY_MIXED);
        } else {
            sema_error(&f->name_tok, "field $%s needs a type annotation", f->name);
        }
    }
    /* methods */
    for (size_t i = 0; i < c->methods.len; i++) {
        AstFn *m = c->methods.items[i];
        m->cls = c;
        if (strcmp(m->name, "__construct") == 0) m->is_ctor = true;
        check_fn_body(&sc, m, sc.self_type);
    }
    /* interface methods must be abstract */
    if (c->is_interface) {
        for (size_t i = 0; i < c->methods.len; i++) {
            AstFn *m = c->methods.items[i];
            if (m->body.len) {
                /* allowed (default impl) */
            }
        }
    }
}

void sema_run(Program *prog) {
    builtins_init();
    sema_ok = true;
    s_this = intern("this");
    s_argc = intern("argc");
    s_argv = intern("argv");
    declare_decls(prog);

    /* classes */
    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *d = prog->decls.items[i];
        if (d->kind == DK_CLASS || d->kind == DK_INTERFACE || d->kind == DK_TRAIT)
            check_class(d->u.cls);
    }
    /* free functions */
    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *d = prog->decls.items[i];
        if (d->kind == DK_FN)
            check_fn_body(NULL, d->u.fn, NULL);
    }
    /* top-level statements (the script part) */
    Scope sc;
    scope_init(&sc, NULL);
    check_block(&sc, prog->stmts);
}

const FnSig *sema_fn_sig(AstFn *fn) { return fn_make_sig(fn); }
Type *sema_typeof(Expr *e) { return (Type *)e->type; }
