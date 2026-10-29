with open('src/hphpc/main.c', 'r', encoding='utf-8') as f:
    c = f.read()

target = 'char *link = fmt("gcc %s %s -o %s -lm -lpthread",'
replacement = 'char *link = fmt("gcc %s %s -o %s -lm -lpthread -lsqlite3 -luring",'
assert target in c, 'target not found'

c = c.replace(target, replacement)
with open('src/hphpc/main.c', 'w', encoding='utf-8', newline='\n') as f:
    f.write(c)

print('Updated main.c with -lsqlite3 -luring')
