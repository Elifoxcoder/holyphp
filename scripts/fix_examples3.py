import io, re

p = 'scripts/builtin-examples.js'
s = io.open(p, encoding='utf-8').read()

# Rebuild the whole addslashes entry lines by line index (line 561, 0-based 560)
lines = s.split('\n')
idx = None
for i, l in enumerate(lines):
    if 'addslashes' in l and 'ex:' in l:
        idx = i
        break
assert idx is not None, "addslashes ex line not found"
print("replacing line", idx + 1)

# Derive the new content with explicit character construction
D = chr(34)    # "
Q = chr(39)    # '
B = chr(92)    # \

# JS source we want on the ex line:
#     ex: 'echo addslashes("it\'s \"quoted\"");',
ex_line = "    ex: " + Q + "echo addslashes(" + D + "it" + B + Q + "s " + B + D + "quoted" + B + D + D + ");" + Q + ","

# JS source we want on the out line (value: it\'s \\\"quoted\\\" in the output text):
#     out: "it\\'s \\\"quoted\\\"",
out_line = "    out: " + D + "it" + B + B + Q + "s " + B + B + B + D + "quoted" + B + B + B + D + D + ","

lines[idx] = ex_line
# the glued line currently holds both ex and out; insert the out line after
if 'out:' in lines[idx + 1] or 'nout' in lines[idx + 1]:
    lines[idx + 1] = out_line
else:
    lines.insert(idx + 1, out_line)

io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print("written")
