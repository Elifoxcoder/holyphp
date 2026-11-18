#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static inline void *mem_find(const void *hay, size_t hay_len, const void *needle, size_t needle_len) {
    if (needle_len == 0 || hay_len < needle_len) return NULL;
    const char *h = (const char *)hay;
    const char first = ((const char *)needle)[0];
    const char *end = h + hay_len - needle_len;
    const char *p = h;
    while (p <= end) {
        p = (const char *)memchr(p, first, (size_t)(end - p) + 1);
        if (!p) return NULL;
        if (memcmp(p, needle, needle_len) == 0) return (void *)p;
        p++;
    }
    return NULL;
}

int main(void) {
    const char *req = "POST /posts HTTP/1.1\r\nHost: x\r\nContent-Length: 24\r\n\r\n{\"author\":\"zz\",\"content\":\"cc\"}";
    size_t len = strlen(req);
    char *hdr_end_p = (char *)mem_find(req, len, "\r\n\r\n", 4);
    printf("hdr_end=%p\n", (void*)hdr_end_p);
    size_t hdr_len = (size_t)(hdr_end_p - req) + 4;
    printf("hdr_len=%zu\n", hdr_len);
    char *cl = (char *)mem_find(req, hdr_len, "\r\nContent-Length: ", 18);
    size_t body_len = cl ? (size_t)strtoul(cl + 18, NULL, 10) : 0;
    printf("cl=%p body_len=%zu\n", (void*)cl, body_len);
    const char *body = req + hdr_len;
    char *a = (char *)mem_find(body, body_len, "\"author\":\"", 10);
    printf("a=%p\n", (void*)a);
    if (a) {
        char *ap = a + 10;
        char *ae = (char *)mem_find(ap, (size_t)(body + body_len - ap), "\"", 1);
        printf("author=%.*s\n", (int)(ae-ap), ap);
    }
    return 0;
}
