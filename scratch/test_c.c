#include <stdio.h>
#include <liburing.h>
#include <sqlite3.h>

int main() {
    struct io_uring ring;
    int ret = io_uring_queue_init(64, &ring, 0);
    printf("io_uring: %d, sqlite3: %s\n", ret, sqlite3_libversion());
    io_uring_queue_exit(&ring);
    return 0;
}
