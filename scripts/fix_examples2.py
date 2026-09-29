import io, re

p = 'scripts/builtin-examples.js'
s = io.open(p, encoding='utf-8').read()

# The five entries appended via heredoc have ex: strings with REAL newlines
# inside single-quoted JS strings -> invalid. Join them into one line with \n.
lines = s.split('\n')
out = []
i = 0
fixed = 0
while i < len(lines):
    l = lines[i]
    m = re.match(r"^(\s*ex: ')(.*)$", l)
    # an ex: line that does NOT close its quote on this line
    if m and l.count("'") % 2 == 1:
        # gather continuation lines until the quote closes
        parts = [m.group(2)]
        j = i + 1
        while j < len(lines):
            nxt = lines[j]
            parts.append(nxt.strip())
            if nxt.count("'") % 2 == 1:
                # this line closes the string; strip the trailing ', or ';
                tail = parts[-1]
                tail = re.sub(r"'\s*,\s*$", "", tail)
                parts[-1] = tail
                j += 1
                break
            j += 1
        joined = m.group(1) + "\\n".join(parts) + "',"
        out.append(joined)
        fixed += 1
        i = j
        continue
    out.append(l)
    i += 1

s2 = '\n'.join(out)
io.open(p, 'w', encoding='utf-8', newline='').write(s2)
print("multiline ex fixed:", fixed)
