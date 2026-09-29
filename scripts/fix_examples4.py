import io, re

p = 'scripts/builtin-examples.js'
s = io.open(p, encoding='utf-8').read()
lines = s.split('\n')

# A glued line contains TWO properties on one physical line because the
# multiline joiner merged them (literal backslash-n + "out:" got swallowed).
# Detect the glued sequence  Q ","  B "n out:"  and split it back into two lines.
D = chr(34)   # "
Q = chr(39)   # '
B = chr(92)   # \

glue = Q + "," + B + "nout: "
out = []
split_count = 0
for l in lines:
    while glue in l:
        pos = l.index(glue)
        first = l[:pos + 1] + ","          # keep the closing quote + comma
        rest = "    out: " + l[pos + len(glue):]
        out.append(first)
        l = rest
        split_count += 1
    out.append(l)

print("split glued lines:", split_count)
io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print("written")
