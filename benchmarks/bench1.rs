// bench1.rs — identical workloads to bench1.hphp / bench1.php / bench1.c
// Built with: rustc -O bench1.rs -o bench1.exe
use std::time::Instant;

fn fib(n: i64) -> i64 {
    if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

fn main() {
    // fib(30)
    let t0 = Instant::now();
    let r = fib(30);
    let t1 = Instant::now();
    println!("fib(30) = {} in {:.1} ms", r, (t1 - t0).as_secs_f64() * 1000.0);

    // 1M appends + sum (Vec = amortized growth like PHP arrays)
    let t0 = Instant::now();
    let mut a: Vec<i64> = Vec::with_capacity(0);
    for i in 0..1_000_000i64 {
        a.push((i.wrapping_mul(2_654_435_761)).rem_euclid(1_000_000_007));
    }
    let sum: i64 = a.iter().sum();
    let t1 = Instant::now();
    println!("1M appends: sum={} in {:.1} ms", sum, (t1 - t0).as_secs_f64() * 1000.0);

    // sort 1M
    let t0 = Instant::now();
    a.sort_unstable();
    let t1 = Instant::now();
    println!("sort 1M ints in {:.1} ms", (t1 - t0).as_secs_f64() * 1000.0);

    // 50k concats (String = amortized growth like the hphp builder path)
    let t0 = Instant::now();
    let mut s = String::new();
    for _ in 0..50_000 {
        s.push('x');
    }
    let t1 = Instant::now();
    println!("50k concats: len={} in {:.1} ms", s.len(), (t1 - t0).as_secs_f64() * 1000.0);
}
