import io, re

p = 'scripts/builtin-examples.js'
s = io.open(p, encoding='utf-8').read()

# Object-literal properties must end with a comma, not a semicolon.
# My first draft ended many lines with ";" (C habit). Fix: any line that
# looks like `identifier: <value>;` gets the trailing ";" swapped to ",".
# The value can contain semicolons (hphp code), so anchor at line start/end.
pat = re.compile(r'^(\s+[A-Za-z_][A-Za-z0-9_]*\s*:\s*.+);$', re.M)

count = 0
def repl(m):
    global count
    count += 1
    return m.group(1) + ','
s = pat.sub(repl, s)
print("semicolon->comma fixes:", count)

# Escape bare apostrophes inside double-quoted strings (they are fine for JS
# but make the docs confusing) - actually those were NOT the parse problem;
# skip. The real parse blockers were only the semicolons.

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print("written")
