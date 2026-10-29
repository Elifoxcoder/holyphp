with open('src/hphpc/runtime/hphp_std.c', 'r', encoding='utf-8') as f:
    c = f.read()

bad_affinity = """    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);"""

if bad_affinity in c:
    c = c.replace(bad_affinity, "    // 1 vCPU environment: scheduler runs pinned to the core")
    with open('src/hphpc/runtime/hphp_std.c', 'w', encoding='utf-8', newline='\n') as f:
        f.write(c)
    print('Fixed affinity in hphp_std.c')
