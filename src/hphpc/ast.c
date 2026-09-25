#include "ast.h"

Expr *expr_new(ExprKind k, Token tok) {
    Expr *e = xcalloc(1, sizeof(Expr));
    e->kind = k;
    e->tok = tok;
    return e;
}

Stmt *stmt_new(StmtKind k, Token tok) {
    Stmt *s = xcalloc(1, sizeof(Stmt));
    s->kind = k;
    s->tok = tok;
    return s;
}

Type *type_new(TypeKind k) {
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = k;
    return t;
}

bool type_is_ref(TypeKind k) { return k == TY_REF || k == TY_MUTREF; }

bool type_is_numeric(TypeKind k) {
    switch (k) {
    case TY_INT: case TY_I8: case TY_I16: case TY_I32: case TY_I64:
    case TY_FLOAT: case TY_F32: case TY_F64:
        return true;
    default:
        return false;
    }
}

bool type_is_integral(TypeKind k) {
    switch (k) {
    case TY_INT: case TY_I8: case TY_I16: case TY_I32: case TY_I64:
    case TY_BOOL:
        return true;
    default:
        return false;
    }
}

bool type_is_pointerish(TypeKind k) {
    return k == TY_REF || k == TY_MUTREF || k == TY_PTR;
}

bool type_is_smart_ptr(TypeKind k) {
    return k == TY_OWN || k == TY_RC;
}

bool type_is_value(TypeKind k) {
    switch (k) {
    case TY_INT: case TY_I8: case TY_I16: case TY_I32: case TY_I64:
    case TY_FLOAT: case TY_F32: case TY_F64:
    case TY_BOOL: case TY_ENUM:
        return true;
    default:
        return false;
    }
}

const char *type_kind_name(TypeKind k) {
    switch (k) {
    case TY_ERROR: return "<error>";
    case TY_INT: return "int";
    case TY_I8: return "i8"; case TY_I16: return "i16";
    case TY_I32: return "i32"; case TY_I64: return "i64";
    case TY_FLOAT: return "float";
    case TY_F32: return "f32"; case TY_F64: return "f64";
    case TY_BOOL: return "bool";
    case TY_STRING: return "string";
    case TY_VOID: return "void";
    case TY_MIXED: return "mixed";
    case TY_ARRAY: return "array";
    case TY_VEC: return "Vec";
    case TY_MAP: return "Map";
    case TY_SET: return "Set";
    case TY_REF: return "&";
    case TY_MUTREF: return "&mut";
    case TY_PTR: return "*";
    case TY_OWN: return "own";
    case TY_RC: return "Rc";
    case TY_STRUCT: return "struct";
    case TY_CLASS: return "class";
    case TY_INTERFACE: return "interface";
    case TY_ENUM: return "enum";
    case TY_FN: return "fn";
    case TY_TUPLE: return "tuple";
    case TY_OPTION: return "Option";
    case TY_RESULT: return "Result";
    case TY_SELF: return "self";
    }
    return "?";
}

char *type_to_string(const Type *t) {
    if (!t) return xstrdup("?");
    Buf b; buf_init(&b);
    switch (t->kind) {
    case TY_ARRAY: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "[%s]", e); free(e);
        break;
    }
    case TY_VEC: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "Vec<%s>", e); free(e);
        break;
    }
    case TY_MAP: {
        char *k = type_to_string(t->key), *v = type_to_string(t->val);
        buf_printf(&b, "Map<%s, %s>", k, v); free(k); free(v);
        break;
    }
    case TY_SET: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "Set<%s>", e); free(e);
        break;
    }
    case TY_REF: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "&%s", e); free(e);
        break;
    }
    case TY_MUTREF: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "&mut %s", e); free(e);
        break;
    }
    case TY_PTR: {
        char *e = t->elem ? type_to_string(t->elem) : xstrdup("u8");
        buf_printf(&b, "*%s", e); free(e);
        break;
    }
    case TY_OWN: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "own %s", e); free(e);
        break;
    }
    case TY_RC: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "Rc<%s>", e); free(e);
        break;
    }
    case TY_OPTION: {
        char *e = type_to_string(t->elem);
        buf_printf(&b, "Option<%s>", e); free(e);
        break;
    }
    case TY_RESULT: {
        char *o = type_to_string(t->ok), *e = type_to_string(t->err);
        buf_printf(&b, "Result<%s, %s>", o, e); free(o); free(e);
        break;
    }
    case TY_FN: {
        buf_puts(&b, "fn(");
        for (size_t i = 0; i < t->fn.nparams; i++) {
            if (i) buf_puts(&b, ", ");
            char *p = type_to_string(t->fn.params[i]);
            buf_puts(&b, p); free(p);
        }
        buf_puts(&b, ")");
        if (t->fn.ret && t->fn.ret->kind != TY_VOID) {
            char *r = type_to_string(t->fn.ret);
            buf_printf(&b, " -> %s", r); free(r);
        }
        break;
    }
    case TY_STRUCT: case TY_CLASS: case TY_INTERFACE: case TY_ENUM:
        buf_puts(&b, t->name ? t->name : "?");
        break;
    default:
        buf_puts(&b, type_kind_name(t->kind));
        break;
    }
    return buf_take(&b);
}
