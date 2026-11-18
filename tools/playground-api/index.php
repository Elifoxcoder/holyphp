<?php
declare(strict_types=1);

/**
 * HolyPHP playground API.
 *
 *   POST /api/run    compile and run the submitted program (optional
 *                    "stdin" field = one line per input() call)
 *   POST /api/check  type/borrow check it without running
 *
 * The API runs as `hphpplay` inside its systemd unit. Source and stdin are
 * staged on the capped RAM workspace; a root-owned launcher runs each job
 * in a short-lived, resource-limited Docker container. Output and requests
 * are capped here before the workspace is removed.
 */

const MAX_CODE   = 65536;   // 64 KiB of source
const MAX_STDIN  = 8192;    // 8 KiB of program input (input() lines)
const RATE_LIMIT = 5;       // requests per IP per minute; nginx adds edge limits
const CHECK_RATE_LIMIT = 20; // debounced editor diagnostics; checks never execute code

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('X-Content-Type-Options: nosniff');

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
$path   = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH) ?? '/';

if ($method === 'OPTIONS') {
    http_response_code(204);
    exit;
}
if ($path !== '/api/run' && $path !== '/api/check') {
    http_response_code(404);
    fail('Not found');
}
if ($method !== 'POST') {
    http_response_code(405);
    fail('This endpoint only accepts POST');
}
// nginx is the only client of this socket, so REMOTE_ADDR would always be
// 127.0.0.1 and every visitor would share one bucket. The real address arrives
// in X-Real-IP, which nginx sets from the connection it accepted.
$ip = $_SERVER['HTTP_X_REAL_IP'] ?? $_SERVER['REMOTE_ADDR'] ?? 'unknown';
$ip = preg_replace('/[^0-9a-fA-F.:%-]/', '', $ip) ?: 'unknown';
$rateKey = $ip . '|' . $path;
$rateLimit = $path === '/api/check' ? CHECK_RATE_LIMIT : RATE_LIMIT;
if (!rate_ok($rateKey, $rateLimit)) {
    http_response_code(429);
    fail('Too many requests — wait a minute and try again.');
}

$raw     = file_get_contents('php://input');
$payload = json_decode($raw === false ? '' : $raw, true);
if (!is_array($payload)) {
    fail('Invalid JSON body');
}

$code = isset($payload['code']) && is_string($payload['code']) ? $payload['code'] : '';
if ($code === '') {
    fail('No code provided');
}
if (strlen($code) > MAX_CODE) {
    fail('Code is too long (limit: 64 KiB)');
}
if (($blocked = blocked_playground_feature($code)) !== null) {
    http_response_code(403);
    fail(playground_policy_message($blocked));
}

$name = isset($payload['filename']) && is_string($payload['filename']) ? basename($payload['filename']) : 'main.hphp';
if (!preg_match('/^[A-Za-z0-9_.-]{1,64}\.hphp$/', $name)) {
    $name = 'main.hphp';
}

$dir = '/var/lib/hphp-playground/jobs/api/hphp_' . bin2hex(random_bytes(8));
if (!@mkdir($dir, 0700)) {
    fail('Could not create a workspace — please retry.');
}
$file = $dir . '/' . $name;
if (@file_put_contents($file, $code) === false) {
    cleanup($dir);
    fail('Could not write the source file');
}

$mode = $path === '/api/run' ? 'run' : 'check';
$cmd = ['/usr/bin/sudo', '-n', '/usr/local/sbin/hphp-playground-container',
        'api', $mode, $dir, $name];
$stdinFile = '/dev/null';
if ($mode === 'run') {
    // input() reads stdin. Hand the program the submitted lines, or an empty
    // file when there are none — that way a read hits EOF immediately instead
    // of blocking on the php -S worker until the timeout fires.
    $stdin     = isset($payload['stdin']) && is_string($payload['stdin']) ? $payload['stdin'] : '';
    $stdinFile = $dir . '/stdin.txt';
    if (@file_put_contents($stdinFile, substr($stdin, 0, MAX_STDIN)) === false) {
        cleanup($dir);
        fail('Could not write the program input');
    }
}

$compileSlot = acquire_compile_slot();
if ($compileSlot === false) {
    cleanup($dir);
    http_response_code(429);
    fail('The playground is busy — wait a moment and try again.');
}

$pipes = [];
$proc = proc_open($cmd, [
    0 => ['file', $stdinFile, 'r'],
    1 => ['pipe', 'w'],
    2 => ['redirect', 1],
], $pipes);
if (!is_resource($proc)) {
    flock($compileSlot, LOCK_UN);
    fclose($compileSlot);
    cleanup($dir);
    fail('Could not start the isolated playground container.');
}
$text = '';
$outputTruncated = false;
while (!feof($pipes[1])) {
    $chunk = fread($pipes[1], 8192);
    if ($chunk === false || $chunk === '') {
        break;
    }
    $remaining = 65536 - strlen($text);
    if ($remaining > 0) {
        $text .= substr($chunk, 0, $remaining);
    }
    if (strlen($chunk) > max(0, $remaining)) {
        $outputTruncated = true;
    }
}
fclose($pipes[1]);
$rc = proc_close($proc);
flock($compileSlot, LOCK_UN);
fclose($compileSlot);
cleanup($dir);

if ($outputTruncated) {
    $marker = "\n… (output truncated)";
    $text = substr($text, 0, 65536 - strlen($marker)) . $marker;
}

if ($rc === 0) {
    if ($text === '') {
        $text = $mode === 'check' ? 'OK — types and borrows check out.' : '(no output)';
    }
    echo json_encode(['success' => true, 'output' => $text]);
    exit;
}

fail($text === '' ? 'Execution failed (exit code ' . $rc . ')' : $text, $rc);

// ---------------------------------------------------------------------------

function fail(string $message, int $code = 0): void
{
    echo json_encode(['success' => false, 'error' => $message, 'exitCode' => $code]);
    exit;
}

function cleanup(string $dir): void
{
    if (!is_dir($dir)) {
        return;
    }
    $it = new RecursiveIteratorIterator(
        new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS),
        RecursiveIteratorIterator::CHILD_FIRST
    );
    foreach ($it as $entry) {
        $entry->isDir() ? @rmdir($entry->getPathname()) : @unlink($entry->getPathname());
    }
    @rmdir($dir);
}

/** Return the first restricted token found in code, ignoring comments/strings. */
function blocked_playground_feature(string $code): ?string
{
    $blocked = array_fill_keys([
        'unsafe', 'malloc', 'free', 'memcpy', 'memset', 'ffi_load', 'ffi_call', 'heap_dump',
        'shell_exec', 'exec', 'passthru', 'proc_open', 'proc_get_status', 'proc_start',
        'proc_read', 'proc_read_err', 'proc_write', 'proc_alive', 'proc_exit', 'proc_close',
        'proc_kill', 'system', 'popen', 'pcntl_exec', 'call_user_func',
        'holy_maxout_start', 'holy_challenge_start', 'thr_spawn', 'thr_parallel_map',
        'file_put_contents', 'fopen', 'fwrite', 'fclose', 'mkdir', 'rmdir', 'unlink',
        'rename', 'copy', 'touch',
    ], true);
    $codeOnly = '';
    foreach (token_get_all('<?php ' . $code) as $token) {
        if (is_array($token)) {
            if ($token[0] === T_CLOSE_TAG || $token[0] === T_OPEN_TAG_WITH_ECHO) {
                return 'PHP tag syntax';
            }
            if ($token[0] === T_INLINE_HTML
                && preg_match('/\b(' . implode('|', array_keys($blocked)) . ')\b/i', $token[1], $match)) {
                return strtolower($match[1]);
            }
            if ($token[0] === T_COMMENT || $token[0] === T_DOC_COMMENT
                || $token[0] === T_CONSTANT_ENCAPSED_STRING
                || $token[0] === T_ENCAPSED_AND_WHITESPACE) {
                continue;
            }
            $codeOnly .= $token[1];
            if ($token[0] === T_STRING) {
                $name = strtolower($token[1]);
                if (isset($blocked[$name])) {
                    return $name;
                }
            }
        } elseif ($token === '`') {
            return 'backtick shell syntax';
        } else {
            $codeOnly .= $token;
        }
    }
    if (preg_match('/\bwhile\s*\(\s*\(*\s*(?:true|1)\s*\)*\s*\)/i', $codeOnly)) {
        return 'unbounded while loop';
    }
    if (preg_match('/\bfor\s*\(\s*;\s*;\s*\)/i', $codeOnly)) {
        return 'unbounded for loop';
    }
    return null;
}

function playground_policy_message(string $feature): string
{
    return 'Not allowed in the online playground: ' . $feature
        . ' is restricted. Shell/process calls, raw memory/FFI, file writes and unconditional loops are disabled here.';
}

/** Allow at most two compiler/program jobs across both API services. */
function acquire_compile_slot()
{
    for ($i = 0; $i < 2; $i++) {
        $handle = @fopen('/run/hphp-playground/compile-slot-' . $i . '.lock', 'c');
        if ($handle !== false && flock($handle, LOCK_EX | LOCK_NB)) {
            return $handle;
        }
        if ($handle !== false) {
            fclose($handle);
        }
    }
    return false;
}

/** Fixed-window counter: RATE_LIMIT requests per IP per minute. */
function rate_ok(string $ip, int $limit): bool
{
    $base = sys_get_temp_dir() . '/hphp_rate';
    if (!is_dir($base)) {
        @mkdir($base, 0700);
    }
    $handle = @fopen($base . '/' . sha1($ip) . '.json', 'c+');
    if ($handle === false) {
        return true; // a broken counter must not block the playground
    }

    $window = intdiv(time(), 60);
    $count  = 1;

    flock($handle, LOCK_EX);
    $data = json_decode((string) stream_get_contents($handle), true);
    if (is_array($data) && ($data['window'] ?? -1) === $window) {
        $count = (int) $data['count'] + 1;
    }
    ftruncate($handle, 0);
    rewind($handle);
    fwrite($handle, json_encode(['window' => $window, 'count' => $count]));
    flock($handle, LOCK_UN);
    fclose($handle);

    return $count <= $limit;
}
