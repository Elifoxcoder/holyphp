import io

p = 'scripts/builtin-examples.js'
s = io.open(p, encoding='utf-8').read()

# The appended tail block (str_repeat .. malloc) went through two sloppy fix
# passes. Rewrite its ex/out lines wholesale with known-good content.
D = chr(34)   # "
Q = chr(39)   # '
B = chr(92)   # \
N = B + 'n'   # JS \n escape inside a string literal

def ex(lines):
    return "  ex: " + Q + N.join(lines) + Q + ","

fixes = [
    # str_repeat
    ("  ex: " + Q + "echo str_repeat(" + D + "ab" + D + ", 3),\\necho " + D + "[" + D + " . str_repeat(" + D + "-" + D + ", 5) . " + D + "]" + D + ";';',",
     ex(['echo str_repeat("ab", 3);', 'echo "[" . str_repeat("-", 5) . "]";'])),
    ("  out: " + D + "ababab\n[-----]" + D + ",",
     '  out: "ababab' + N + '[-----]",'),
    # explode
    ("  ex: " + Q + "$p = explode(" + D + "," + D + ", " + D + "a,b,c" + D + "),\\necho count($p), " + D + " " + D + ", $p[1];\\necho explode(" + D + "/" + D + ", " + D + "2024/03/15" + D + ")[2];';',",
     ex(['$p = explode(",", "a,b,c");', 'echo count($p), " ", $p[1];', 'echo explode("/", "2024/03/15")[2];'])),
    ("  out: " + D + "3 b\n15" + D + ",",
     '  out: "3 b' + N + '15",'),
    # str_split
    ("  ex: " + Q + "echo implode(" + D + "|" + D + ", str_split(" + D + "abc" + D + ")),\\necho implode(" + D + "|" + D + ", str_split(" + D + "abcdef" + D + ", 2));';',",
     ex(['echo implode("|", str_split("abc"));', 'echo implode("|", str_split("abcdef", 2));'])),
    ("  out: " + D + "a|b|c\nab|cd|ef" + D + ",",
     '  out: "a|b|c' + N + 'ab|cd|ef",'),
    # glob
    ("  ex: " + Q + "$f = glob(" + D + "tests/positive/*.hphp" + D + "),\\necho count($f) >= 5 ? " + D + "found-tests" + D + " : " + D + "none" + D + ";';',",
     ex(['$f = glob("tests/positive/*.hphp");', 'echo count($f) >= 5 ? "found-tests" : "none";'])),
    ("  out: " + D + "found-tests\nnone" + D + ",",
     '  out: "found-tests",'),
]

applied = 0
for old, new in fixes:
    if old in s:
        s = s.replace(old, new)
        applied += 1
    else:
        print("NOT FOUND:", old[:70])

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print("applied:", applied, "/", len(fixes))
