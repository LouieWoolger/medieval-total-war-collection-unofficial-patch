#ifndef MTW_PATCH_JSON_H
#define MTW_PATCH_JSON_H
/* Ordered, bounded JSON for the Windows PowerShell 5.1 wire format.
   All strings have explicit UTF-8 lengths, including object keys. A document
   owns nodes, strings and writer buffers through its context. Do not copy it.
   Serialization is shallow-const: it leaves the tree untouched and adds its
   output buffer to the document's mutable allocation arena. */
#include "patch_identity.h"
#include <stdio.h>

#define JSON_MAX_BYTES ((size_t)2097152)
#define JSON_MAX_NODES ((size_t)100000)
#define JSON_MAX_DEPTH 40
typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;
typedef struct JsonDocument JsonDocument;
typedef struct JsonValue {
    JsonDocument *document;
    struct JsonValue *parent;
    JsonType type;
    int boolean;
    int64_t number;
    char *string;
    size_t length;
    char *name;
    size_t name_length;
    struct JsonValue *child, *last, *next;
    size_t count;
} JsonValue;
struct JsonDocument {
    PatchContext context;
    PatchContext *arena; /* mutable pointee used by shallow-const serialization */
    JsonValue *root;
    size_t nodes;
};

static inline int json_invalid(PatchError *error, const char *message) {
    patch_error_set(error, "receipt_invalid", message, ERROR_INVALID_DATA);
    return 0;
}
static inline void json_document_close(JsonDocument *document) {
    if (!document)
        return;
    patch_context_close(&document->context);
    document->root = NULL;
    document->nodes = 0;
    document->arena = NULL;
}
static inline int json_utf8_valid(const char *s, size_t n) {
    size_t pos = 0;
    uint32_t scalar;
    if (!s && n)
        return 0;
    while (pos < n)
        if (!patch_utf8_scalar((const unsigned char *)s, n, &pos, &scalar))
            return 0;
    return 1;
}
static inline int json_copy_string(JsonDocument *doc, const char *s, size_t n, char **out, PatchError *e) {
    void *p = NULL;
    *out = NULL;
    if (n > JSON_MAX_BYTES || !json_utf8_valid(s, n))
        return json_invalid(e, "Invalid or oversized JSON string.");
    if (!patch_alloc(&doc->context, n + 1, 1, &p, e))
        return 0;
    if (n)
        memcpy(p, s, n);
    *out = (char *)p;
    return 1;
}
static inline JsonValue *json_new(JsonDocument *doc, JsonType type, PatchError *e) {
    void *p = NULL;
    JsonValue *v;
    if (!doc || type < JSON_NULL || type > JSON_OBJECT || doc->nodes >= JSON_MAX_NODES) {
        json_invalid(e, "JSON node limit or invalid type.");
        return NULL;
    }
    if (!patch_alloc(&doc->context, 1, sizeof(JsonValue), &p, e))
        return NULL;
    ++doc->nodes;
    doc->arena = &doc->context;
    v = (JsonValue *)p;
    v->document = doc;
    v->type = type;
    return v;
}
static inline JsonValue *json_string(JsonDocument *doc, const char *s, size_t n, PatchError *e) {
    JsonValue *v = json_new(doc, JSON_STRING, e);
    if (!v || !json_copy_string(doc, s, n, &v->string, e))
        return NULL;
    v->length = n;
    return v;
}
static inline JsonValue *json_text(JsonDocument *d, const char *s, PatchError *e) {
    return json_string(d, s, strlen(s), e);
}
static inline JsonValue *json_number(JsonDocument *d, int64_t n, PatchError *e) {
    JsonValue *v = json_new(d, JSON_NUMBER, e);
    if (v)
        v->number = n;
    return v;
}
static inline JsonValue *json_bool(JsonDocument *d, int b, PatchError *e) {
    JsonValue *v = json_new(d, JSON_BOOL, e);
    if (v)
        v->boolean = !!b;
    return v;
}
static inline JsonValue *json_get_n(const JsonValue *object, const char *name, size_t n) {
    JsonValue *v;
    if (!object || object->type != JSON_OBJECT)
        return NULL;
    for (v = object->child; v; v = v->next)
        if (v->name_length == n && !memcmp(v->name, name, n))
            return v;
    return NULL;
}
static inline JsonValue *json_get(const JsonValue *object, const char *name) {
    return json_get_n(object, name, strlen(name));
}
static inline int json_is_text(const JsonValue *v, const char *s) {
    return v && v->type == JSON_STRING && v->length == strlen(s) && !memcmp(v->string, s, v->length);
}
static inline const char *json_cstring(const JsonValue *v, PatchError *e) {
    if (!v || v->type != JSON_STRING || memchr(v->string, 0, v->length)) {
        json_invalid(e, "Expected a string without embedded NUL.");
        return NULL;
    }
    return v->string;
}
static inline int json_append(JsonValue *array, JsonValue *v, PatchError *e) {
    JsonValue *ancestor;
    if (!array || array->type != JSON_ARRAY || !v || v->next || v->parent || v->document != array->document)
        return json_invalid(e, "Invalid JSON array append.");
    for (ancestor = array; ancestor; ancestor = ancestor->parent)
        if (ancestor == v)
            return json_invalid(e, "Cyclic JSON attachment.");
    if (array->last)
        array->last->next = v;
    else
        array->child = v;
    array->last = v;
    v->parent = array;
    ++array->count;
    return 1;
}
/* Values must be fresh unattached nodes. Replacement preserves field position. */
static inline int json_set_n(JsonDocument *d, JsonValue *object, const char *key, size_t n, JsonValue *v,
                             PatchError *e) {
    JsonValue *old, *previous = NULL, *ancestor;
    char *name = NULL;
    if (!object || object->type != JSON_OBJECT || !v || v->next || v->parent || v->document != d ||
        object->document != d)
        return json_invalid(e, "Invalid JSON object assignment.");
    for (ancestor = object; ancestor; ancestor = ancestor->parent)
        if (ancestor == v)
            return json_invalid(e, "Cyclic JSON attachment.");
    if (!json_copy_string(d, key, n, &name, e))
        return 0;
    for (old = object->child; old; previous = old, old = old->next)
        if (old->name_length == n && !memcmp(old->name, key, n))
            break;
    v->name = name;
    v->name_length = n;
    v->parent = object;
    if (old) {
        v->next = old->next;
        if (previous)
            previous->next = v;
        else
            object->child = v;
        if (object->last == old)
            object->last = v;
        old->next = NULL;
        old->parent = NULL;
    } else {
        if (object->last)
            object->last->next = v;
        else
            object->child = v;
        object->last = v;
        ++object->count;
    }
    return 1;
}
static inline int json_set(JsonDocument *d, JsonValue *o, const char *k, JsonValue *v, PatchError *e) {
    return json_set_n(d, o, k, strlen(k), v, e);
}
static inline JsonValue *json_clone_at(JsonDocument *d, const JsonValue *v, unsigned depth, PatchError *e) {
    JsonValue *out, *c, *copy;
    if (!v || depth > JSON_MAX_DEPTH) {
        json_invalid(e, "JSON depth limit.");
        return NULL;
    }
    out = json_new(d, v->type, e);
    if (!out)
        return NULL;
    out->boolean = v->boolean;
    out->number = v->number;
    if (v->type == JSON_STRING) {
        if (!json_copy_string(d, v->string, v->length, &out->string, e))
            return NULL;
        out->length = v->length;
    }
    for (c = v->child; c; c = c->next) {
        copy = json_clone_at(d, c, depth + 1, e);
        if (!copy)
            return NULL;
        if (v->type == JSON_ARRAY) {
            if (!json_append(out, copy, e))
                return NULL;
        } else if (v->type == JSON_OBJECT) {
            if (!json_set_n(d, out, c->name, c->name_length, copy, e))
                return NULL;
        } else {
            json_invalid(e, "Scalar JSON has children.");
            return NULL;
        }
    }
    return out;
}
static inline JsonValue *json_clone(JsonDocument *d, const JsonValue *v, PatchError *e) {
    return json_clone_at(d, v, 0, e);
}

typedef struct {
    const unsigned char *text;
    size_t size, pos;
    JsonDocument *doc;
    PatchError *error;
} JsonParser;
static inline void json_space(JsonParser *p) {
    while (p->pos < p->size && (p->text[p->pos] == ' ' || p->text[p->pos] == '\t' ||
                                p->text[p->pos] == '\r' || p->text[p->pos] == '\n'))
        ++p->pos;
}
static inline int json_hex(JsonParser *p, uint32_t *out) {
    unsigned i;
    uint32_t n = 0;
    if (p->size - p->pos < 4)
        return 0;
    for (i = 0; i < 4; ++i) {
        unsigned c = p->text[p->pos++];
        n <<= 4;
        if (c >= '0' && c <= '9')
            n += c - '0';
        else if (c >= 'a' && c <= 'f')
            n += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            n += c - 'A' + 10;
        else
            return 0;
    }
    *out = n;
    return 1;
}
static inline int json_parse_string(JsonParser *p, char **out, size_t *length) {
    size_t begin, end, n = 0;
    void *memory = NULL;
    char *s;
    uint32_t c, y;
    if (p->pos == p->size || p->text[p->pos++] != '"')
        return json_invalid(p->error, "Expected JSON string.");
    begin = p->pos;
    end = begin;
    /* Escapes only shrink UTF-8, so the raw span is a sufficient capacity. */
    while (end < p->size) {
        if (p->text[end] == '"')
            break;
        if (p->text[end++] == '\\' && end < p->size)
            ++end;
    }
    if (end == p->size)
        return json_invalid(p->error, "Unterminated JSON string.");
    if (!patch_alloc(&p->doc->context, end - begin + 1, 1, &memory, p->error))
        return 0;
    s = (char *)memory;
    while (p->pos < end) {
        c = p->text[p->pos++];
        if (c < 32)
            goto bad;
        if (c != '\\') {
            s[n++] = (char)c;
            continue;
        }
        if (p->pos == end)
            goto bad;
        c = p->text[p->pos++];
        switch (c) {
        case '"':
        case '\\':
        case '/':
            s[n++] = (char)c;
            break;
        case 'b':
            s[n++] = '\b';
            break;
        case 'f':
            s[n++] = '\f';
            break;
        case 'n':
            s[n++] = '\n';
            break;
        case 'r':
            s[n++] = '\r';
            break;
        case 't':
            s[n++] = '\t';
            break;
        case 'u':
            if (!json_hex(p, &c))
                goto bad;
            if (c >= 0xd800 && c <= 0xdbff) {
                if (p->size - p->pos < 2 || p->text[p->pos++] != '\\' || p->text[p->pos++] != 'u' ||
                    !json_hex(p, &y) || y < 0xdc00 || y > 0xdfff)
                    goto bad;
                c = 0x10000 + ((c - 0xd800) << 10) + (y - 0xdc00);
            } else if (c >= 0xdc00 && c <= 0xdfff)
                goto bad;
            if (p->pos > end)
                goto bad;
            if (c < 0x80)
                s[n++] = (char)c;
            else if (c < 0x800) {
                s[n++] = (char)(0xc0 | (c >> 6));
                s[n++] = (char)(0x80 | (c & 63));
            } else if (c < 0x10000) {
                s[n++] = (char)(0xe0 | (c >> 12));
                s[n++] = (char)(0x80 | ((c >> 6) & 63));
                s[n++] = (char)(0x80 | (c & 63));
            } else {
                s[n++] = (char)(0xf0 | (c >> 18));
                s[n++] = (char)(0x80 | ((c >> 12) & 63));
                s[n++] = (char)(0x80 | ((c >> 6) & 63));
                s[n++] = (char)(0x80 | (c & 63));
            }
            break;
        default:
            goto bad;
        }
    }
    if (p->pos != end || !json_utf8_valid(s, n))
        goto bad;
    ++p->pos;
    s[n] = 0;
    *out = s;
    *length = n;
    return 1;
bad:
    return json_invalid(p->error, "Malformed JSON string.");
}
static inline uint32_t json_key_hash(const char *s, size_t n) {
    uint32_t h = 2166136261U;
    size_t i;
    for (i = 0; i < n; ++i)
        h = (h ^ (unsigned char)s[i]) * 16777619U;
    return h;
}
/* Per-object hash table bounds duplicate detection without quadratic scans. */
static inline int json_key_insert(JsonParser *p, JsonValue ***table, size_t *capacity, JsonValue *v,
                                  size_t count) {
    size_t cap = *capacity, i, slot;
    void *memory = NULL;
    JsonValue **next;
    if (!cap || count > cap / 2) {
        size_t newcap = cap ? cap * 2 : 16;
        if (!patch_alloc(&p->doc->context, newcap, sizeof(JsonValue *), &memory, p->error))
            return 0;
        next = (JsonValue **)memory;
        for (i = 0; i < cap; ++i)
            if ((*table)[i]) {
                JsonValue *old = (*table)[i];
                slot = json_key_hash(old->name, old->name_length) & (newcap - 1);
                while (next[slot])
                    slot = (slot + 1) & (newcap - 1);
                next[slot] = old;
            }
        patch_context_free(&p->doc->context, *table);
        *table = next;
        *capacity = newcap;
        cap = newcap;
    }
    slot = json_key_hash(v->name, v->name_length) & (cap - 1);
    while ((*table)[slot]) {
        JsonValue *old = (*table)[slot];
        if (old->name_length == v->name_length && !memcmp(old->name, v->name, v->name_length))
            return json_invalid(p->error, "Duplicate JSON key.");
        slot = (slot + 1) & (cap - 1);
    }
    (*table)[slot] = v;
    return 1;
}
static inline JsonValue *json_parse_value(JsonParser *p, unsigned depth) {
    JsonValue *v = NULL, *child;
    unsigned c;
    uint64_t n = 0, limit;
    size_t first, cap = 0;
    int negative = 0;
    JsonValue **keys = NULL;
    json_space(p);
    if (depth > JSON_MAX_DEPTH || p->pos == p->size) {
        json_invalid(p->error, "JSON depth or syntax limit.");
        return NULL;
    }
    c = p->text[p->pos];
    if (c == '"') {
        v = json_new(p->doc, JSON_STRING, p->error);
        if (!v || !json_parse_string(p, &v->string, &v->length))
            return NULL;
        return v;
    }
    if (c == '{' || c == '[') {
        unsigned end = c == '{' ? '}' : ']';
        v = json_new(p->doc, c == '{' ? JSON_OBJECT : JSON_ARRAY, p->error);
        if (!v)
            return NULL;
        ++p->pos;
        json_space(p);
        if (p->pos < p->size && p->text[p->pos] == end) {
            ++p->pos;
            return v;
        }
        for (;;) {
            char *name = NULL;
            size_t len = 0;
            if (c == '{') {
                json_space(p);
                if (!json_parse_string(p, &name, &len))
                    return NULL;
                json_space(p);
                if (p->pos == p->size || p->text[p->pos++] != ':')
                    goto bad;
            }
            child = json_parse_value(p, depth + 1);
            if (!child)
                return NULL;
            child->name = name;
            child->name_length = len;
            child->parent = v;
            if (c == '{' && !json_key_insert(p, &keys, &cap, child, v->count + 1))
                return NULL;
            if (v->last)
                v->last->next = child;
            else
                v->child = child;
            v->last = child;
            ++v->count;
            json_space(p);
            if (p->pos == p->size)
                goto bad;
            if (p->text[p->pos] == end) {
                ++p->pos;
                patch_context_free(&p->doc->context, keys);
                return v;
            }
            if (p->text[p->pos++] != ',')
                goto bad;
        }
    }
    if (p->size - p->pos >= 4 && !memcmp(p->text + p->pos, "null", 4)) {
        p->pos += 4;
        return json_new(p->doc, JSON_NULL, p->error);
    }
    if (p->size - p->pos >= 4 && !memcmp(p->text + p->pos, "true", 4)) {
        p->pos += 4;
        return json_bool(p->doc, 1, p->error);
    }
    if (p->size - p->pos >= 5 && !memcmp(p->text + p->pos, "false", 5)) {
        p->pos += 5;
        return json_bool(p->doc, 0, p->error);
    }
    if (c == '-') {
        negative = 1;
        ++p->pos;
    }
    first = p->pos;
    limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    while (p->pos < p->size && p->text[p->pos] >= '0' && p->text[p->pos] <= '9') {
        unsigned d = p->text[p->pos++] - '0';
        if (n > (limit - d) / 10)
            goto bad;
        n = n * 10 + d;
    }
    if (first == p->pos || (p->pos - first > 1 && p->text[first] == '0'))
        goto bad;
    return json_number(
        p->doc, negative ? (n == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)n) : (int64_t)n, p->error);
bad:
    json_invalid(p->error, "Malformed JSON.");
    return NULL;
}
static inline int json_parse(const char *text, size_t length, JsonDocument *doc, PatchError *error) {
    JsonParser p;
    JsonValue *root;
    if (!doc || doc->root || doc->context.resources) {
        patch_error_set(error, "invalid_argument", "JSON parse requires an empty document.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (length > JSON_MAX_BYTES || !json_utf8_valid(text, length))
        return json_invalid(error, "Invalid UTF-8 or JSON size limit.");
    p.text = (const unsigned char *)text;
    p.size = length;
    p.pos = length >= 3 && !memcmp(text, "\xef\xbb\xbf", 3) ? 3 : 0;
    p.doc = doc;
    p.error = error;
    root = json_parse_value(&p, 0);
    json_space(&p);
    if (!root || p.pos != length) {
        if (root)
            json_invalid(error, "Trailing JSON data.");
        json_document_close(doc);
        return 0;
    }
    doc->root = root;
    return 1;
}

typedef struct {
    char *data;
    size_t length;
    PatchError *error;
    const JsonValue *skip;
} JsonWriter;
static inline int json_emit(JsonWriter *w, const char *s, size_t n) {
    if (n > JSON_MAX_BYTES - w->length)
        return json_invalid(w->error, "Canonical JSON size limit.");
    if (w->data && n)
        memcpy(w->data + w->length, s, n);
    w->length += n;
    return 1;
}
static inline int json_quote(JsonWriter *w, const char *s, size_t n) {
    size_t pos = 0, start;
    uint32_t c;
    char escaped[7];
    const char *special;
    if (!json_emit(w, "\"", 1))
        return 0;
    while (pos < n) {
        start = pos;
        if (!patch_utf8_scalar((const unsigned char *)s, n, &pos, &c))
            return json_invalid(w->error, "Invalid JSON UTF-8.");
        special = NULL;
        switch (c) {
        case '"':
            special = "\\\"";
            break;
        case '\\':
            special = "\\\\";
            break;
        case '\b':
            special = "\\b";
            break;
        case '\t':
            special = "\\t";
            break;
        case '\n':
            special = "\\n";
            break;
        case '\f':
            special = "\\f";
            break;
        case '\r':
            special = "\\r";
            break;
        }
        if (special) {
            if (!json_emit(w, special, 2))
                return 0;
        } else if (c < 32 || c == 0x85 || c == 0x2028 || c == 0x2029 || c == '\'' || c == '<' || c == '>' ||
                   c == '&') {
            snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)c);
            if (!json_emit(w, escaped, 6))
                return 0;
        } else if (!json_emit(w, s + start, pos - start))
            return 0;
    }
    return json_emit(w, "\"", 1);
}
static inline int json_write_at(JsonWriter *w, const JsonValue *v, unsigned depth) {
    const JsonValue *c;
    size_t count = 0;
    char digits[32];
    int n;
    if (!v || depth > JSON_MAX_DEPTH)
        return json_invalid(w->error, "JSON depth limit.");
    switch (v->type) {
    case JSON_NULL:
        return json_emit(w, "null", 4);
    case JSON_BOOL:
        return json_emit(w, v->boolean ? "true" : "false", v->boolean ? 4 : 5);
    case JSON_NUMBER:
        n = snprintf(digits, sizeof(digits), "%lld", (long long)v->number);
        return n > 0 && json_emit(w, digits, (size_t)n);
    case JSON_STRING:
        return json_quote(w, v->string, v->length);
    case JSON_ARRAY:
    case JSON_OBJECT:
        if (!json_emit(w, v->type == JSON_ARRAY ? "[" : "{", 1))
            return 0;
        for (c = v->child; c; c = c->next) {
            if (c == w->skip)
                continue;
            if (++count > JSON_MAX_NODES)
                return json_invalid(w->error, "JSON node limit.");
            if (count > 1 && !json_emit(w, ",", 1))
                return 0;
            if (v->type == JSON_OBJECT && (!json_quote(w, c->name, c->name_length) || !json_emit(w, ":", 1)))
                return 0;
            if (!json_write_at(w, c, depth + 1))
                return 0;
        }
        return json_emit(w, v->type == JSON_ARRAY ? "]" : "}", 1);
    }
    return json_invalid(w->error, "Invalid JSON type.");
}
static inline int json_write_context(PatchContext *context, const JsonValue *v, const JsonValue *skip,
                                     char **out, size_t *length, PatchError *e) {
    JsonWriter w = {0};
    void *p = NULL;
    size_t n;
    if (out)
        *out = NULL;
    if (length)
        *length = 0;
    if (!context || !out || !length) {
        patch_error_set(e, "invalid_argument", "JSON writer output required.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    w.error = e;
    w.skip = skip;
    if (!json_write_at(&w, v, 0))
        return 0;
    n = w.length;
    if (!patch_alloc(context, n + 1, 1, &p, e))
        return 0;
    w.data = (char *)p;
    w.length = 0;
    if (!json_write_at(&w, v, 0)) {
        patch_context_free(context, p);
        return 0;
    }
    *out = (char *)p;
    *length = n;
    return 1;
}
static inline int json_write_canonical(const JsonDocument *doc, char **out, size_t *length, PatchError *e) {
    return json_write_context(doc ? doc->arena : NULL, doc ? doc->root : NULL, NULL, out, length, e);
}
static inline int json_write_value(JsonDocument *owner, const JsonValue *value, const JsonValue *skip,
                                   char **out, size_t *length, PatchError *e) {
    return json_write_context(owner ? &owner->context : NULL, value, skip, out, length, e);
}
static inline int json_integrity(JsonDocument *owner, const JsonValue *value, char digest[65],
                                 PatchError *e) {
    char *bytes = NULL;
    size_t n = 0;
    int ok;
    if (!value || value->type != JSON_OBJECT)
        return json_invalid(e, "Integrity requires a JSON object.");
    if (!json_write_value(owner, value, json_get(value, "integrity_sha256"), &bytes, &n, e))
        return 0;
    ok = patch_sha256_bytes(&owner->context, bytes, n, digest);
    if (!ok)
        patch_error_set(e, owner->context.error.code, owner->context.error.message, GetLastError());
    patch_context_free(&owner->context, bytes);
    return ok;
}
static inline int json_seal(JsonDocument *owner, JsonValue *value, PatchError *e) {
    char digest[65];
    JsonValue *hash;
    if (!json_integrity(owner, value, digest, e))
        return 0;
    hash = json_text(owner, digest, e);
    return hash && json_set(owner, value, "integrity_sha256", hash, e);
}
#endif
