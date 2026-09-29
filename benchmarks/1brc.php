<?php
// 1brc.php — reference twin of benchmarks/1brc.hphp, run with real PHP CLI.
// Same fixed-point tenths accumulation + half-up mean rounding.
//
// Usage: php benchmarks/1brc.php [measurements.txt]

$path = "C:/Users/elias/Downloads/1brc-main/1brc-main/measurements.txt";
if ($argc > 1) {
    $path = $argv[1];
}

$t0 = hrtime(true);

$stats = [];
$fh = fopen($path, "rb");
if ($fh === false) {
    echo "1brc: cannot open {$path}\n";
    exit(1);
}
$rows = 0;
while (($line = fgets($fh)) !== false) {
    $rows++;
    $sep = strpos($line, ";");
    $name = rtrim(substr($line, 0, $sep));
    $temp = floatval(substr($line, $sep + 1));
    $t10 = (int)round($temp * 10.0);
    if (isset($stats[$name])) {
        $s = $stats[$name];
        $c = $s[0] + 1;
        $mn = $s[1];
        $mx = $s[2];
        $sum = $s[3];
        if ($t10 < $mn) {
            $mn = $t10;
        }
        if ($t10 > $mx) {
            $mx = $t10;
        }
        $sum = $sum + $t10;
        $stats[$name] = [$c, $mn, $mx, $sum];
    } else {
        $stats[$name] = [1, $t10, $t10, $t10];
    }
}
fclose($fh);

$t1 = hrtime(true);

$names = array_keys($stats);
sort($names);
$out = "{";
$first = true;
foreach ($names as $nm) {
    if (!$first) {
        $out .= ", ";
    }
    $first = false;
    $s = $stats[$nm];
    $sum = $s[3];
    $c = $s[0];
    $m10 = $sum >= 0 ? intdiv($sum * 2 + $c, $c * 2)
                     : -intdiv(-$sum * 2 + $c, $c * 2);
    $out .= $nm . "=" . sprintf("%.1f", $s[1] / 10.0) . "/"
         . sprintf("%.1f", $m10 / 10.0) . "/" . sprintf("%.1f", $s[2] / 10.0);
}
$out .= "}";
echo $out . "\n";

$ms = ($t1 - $t0) / 1e6;
printf("1brc: %d rows in %.1f ms (%.0f rows/s)\n", $rows, $ms, $rows / ($ms / 1000.0));
if ($rows < 1000000000) {
    $proj = ($t1 - $t0) / 1e6 / $rows * 1000000000.0;
    printf("1brc: projected for 1e9 rows: %.0f s (aggregate loop only)\n", $proj / 1000.0);
}
