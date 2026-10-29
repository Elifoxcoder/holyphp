with open('src/hphpc/runtime/hphp_rt.h', 'r', encoding='utf-8') as f:
    h = f.read()

bad = 'hval hpbi_holy_maxout_start(hval port, hval db_path);\n\n#ifdef __cplusplus\nextern "C" {'
good = '#ifdef __cplusplus\nextern "C" {'
h = h.replace(bad, good)

target = 'hval hpbi_proc_kill(hval h);\n'
if 'hpbi_holy_maxout_start' not in h:
    assert target in h
    h = h.replace(target, target + 'hval hpbi_holy_maxout_start(hval port, hval db_path);\n')

with open('src/hphpc/runtime/hphp_rt.h', 'w', encoding='utf-8', newline='\n') as f:
    f.write(h)

print('Fixed hphp_rt.h')
