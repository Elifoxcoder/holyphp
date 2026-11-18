import io

p = 'src/hphpc/runtime/hphp_std.c'
s = io.open(p, encoding='utf-8', newline='').read()

needle = '    if (len >= 11 && memcmp(req, "POST /posts", 11) == 0) {\n'
rep = needle + (
    '        fprintf(stderr, "DBG post len=%zu hdr=%zu body_len=%zu tail=%.*s\\n",'
    ' len, hdr_len, body_len,'
    ' (int)(len > hdr_len ? len - hdr_len : 0), req + hdr_len);\n'
)
assert s.count(needle) == 1, s.count(needle)
s = s.replace(needle, rep)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
