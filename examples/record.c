/* examples/record.c — hand-written C baseline for record.hphp */
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
static uint64_t now_ns(void){ LARGE_INTEGER f,t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return (uint64_t)(t.QuadPart*1000000000ull/f.QuadPart); }
#else
#include <time.h>
static uint64_t now_ns(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec; }
#endif

int main(void){
    uint64_t t0=now_ns();
    long long sum=0;
    for(long long i=0;i<100000000;i++) sum += i%7;
    uint64_t t1=now_ns();
    double t=0.0;
    for(long long i=1;i<=100000000;i++) t += (double)((i*3)%5) - (double)i/2.0;
    uint64_t t2=now_ns();
    printf("int_loop_100M_ns   = %llu  (sum=%lld)\n",(unsigned long long)(t1-t0),sum);
    printf("mixed_loop_100M_ns = %llu  (t=%.9e)\n",(unsigned long long)(t2-t1),t);
    return 0;
}
