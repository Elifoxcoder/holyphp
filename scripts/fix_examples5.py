import io

p = 'scripts/builtin-examples.js'
s = io.open(p, encoding='utf-8').read()

# The stripslashes out: line still carries a stray quote-comma tail from the
# glued mess:   out: "it's",',     ->   out: "it's",
old = '    out: "it' + chr(39) + 's",' + chr(39) + ','
new = '    out: "it' + chr(39) + 's",'
assert old in s, "pattern missing"
s = s.replace(old, new)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print("cleaned")
