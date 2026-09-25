// benchmap.rs — int-keyed map: 200k inserts + 200k lookups + sum
// (mirrors mapbench.hphp / mapbench.php)
use std::collections::HashMap;
use std::time::Instant;

fn main() {
    let t0 = Instant::now();
    let mut m: HashMap<i64, i64> = HashMap::new();
    for i in 0..200_000i64 {
        m.insert(i, i * 2);
    }
    let mut s: i64 = 0;
    for i in 0..200_000i64 {
        s += m[&i];
    }
    let t1 = Instant::now();
    println!("sum={} in {:.1} ms", s, (t1 - t0).as_secs_f64() * 1000.0);
}
