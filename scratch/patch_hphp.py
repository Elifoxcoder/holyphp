with open('src/hphpc/runtime/hphp_std.c', 'r', encoding='utf-8') as f:
    c = f.read()

target = 'typedef struct {\n    HANDLE proc, thread;'
assert target in c

start = c.index(target)
end_marker = '/* ================= canvas ================= */'
end = c.index(end_marker)

block = c[start:end]
stubs = """#else /* !_WIN32 */
hval hpbi_proc_start(hval cmd, hval cwd) { (void)cmd; (void)cwd; return hp_of_int(0); }
hval hpbi_proc_open(hval cmd, hval cwd) { (void)cmd; (void)cwd; return hp_of_int(0); }
hval hpbi_proc_read(hval h, hval maxn) { (void)h; (void)maxn; return hp_of_str(hp_str_lit("")); }
hval hpbi_proc_read_err(hval h, hval maxn) { (void)h; (void)maxn; return hp_of_str(hp_str_lit("")); }
hval hpbi_proc_write(hval h, hval data) { (void)h; (void)data; return hp_of_int(0); }
hval hpbi_proc_alive(hval h) { (void)h; return hp_of_int(0); }
hval hpbi_proc_exit(hval h) { (void)h; return hp_of_int(-1); }
hval hpbi_proc_close(hval h) { (void)h; return hp_of_int(1); }
hval hpbi_proc_kill(hval h) { (void)h; return hp_of_int(1); }
hval hpbi_proc_get_status(hval h) { (void)h; return hp_of_bool(false); }
#endif /* _WIN32 */

"""

c = c[:start] + '#ifdef _WIN32\n' + block + stubs + c[end:]
with open('src/hphpc/runtime/hphp_std.c', 'w', encoding='utf-8', newline='\n') as f:
    f.write(c)

print('hphp_std.c patched successfully')
