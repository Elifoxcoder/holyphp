#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
static uint64_t now_ns(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull + ts.tv_nsec; }
#endif
#ifdef _WIN32
static uint64_t now_ns(void){ LARGE_INTEGER f,t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return (uint64_t)(t.QuadPart*1000000000ull/f.QuadPart); }
#endif

static int64_t fib(int n){ return n<2 ? n : fib(n-1)+fib(n-2); }

int main(void){
    long long acc=0;
    uint64_t t0=now_ns();
    for(long long i=0;i<200000;i++) acc += i%7;
    uint64_t t1=now_ns();
    int64_t f=fib(24);
    uint64_t t2=now_ns();
    char *s=malloc(30001);
    size_t len=0;
    for(int i=0;i<30000;i++){ s[len++]='x'; }
    s[len]=0;
    uint64_t t3=now_ns();
    printf("int_loop_200k_ns = %llu\n",(unsigned long long)(t1-t0));
    printf("fib24_ns         = %llu\n",(unsigned long long)(t2-t1));
    printf("concat_30k_ns    = %llu\n",(unsigned long long)(t3-t2));
    printf("acc=%lld fib=%lld len=%zu\n",acc,(long long)f,len);
    return 0;
}
