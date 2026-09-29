/* builtin-examples.js — parameters, examples and expected output for every
 * HolyPHP builtin. Consumed by scripts/gendocs.js to generate one page per
 * function under docs/builtin/<name>.html, and by scripts/check-examples.js
 * which actually RUNS every example through ./hphp.exe and compares the output.
 *
 * Shape per entry:
 *   params: [{ n: "name", t: "type", req: bool, d: "description" }]   (req defaults true)
 *   ret:    "plain-english return value description"
 *   ex:     HolyPHP source (without the trailing echo of the marker line — keep
 *           examples deterministic and side-effect free)
 *   out:    exact expected stdout of the example
 *   see:    ["related", "functions"]
 *   nocode: true for entries where no runnable example makes sense (UI loops,
 *           blocking servers, dialogs, unsafe/FFI). They get a documentation
 *           snippet instead of a verified run.
 */
module.exports = {
  /* ============ Strings ============ */
  strlen: {
    params: [{
      n: "v",
      t: "string|array",
      d: "the string (bytes) or array (elements) to measure"
    }],
    ret: "Number of bytes in the string, or number of elements in the array.",
    ex: 'echo strlen("HolyPHP"), "\n";\necho strlen(""), "\n";',
    out: "7\n0",
    see: ["count", "substr", "str_pad"],
  },
  count: {
    params: [{
      n: "v",
      t: "array|string",
      d: "array to count, or string to measure"
    }],
    ret: "Element count of the array, byte length of the string.",
    ex: 'echo count([10, 20, 30]), "\n";\necho count(["a" => 1, "b" => 2]), "\n";',
    out: "3\n2",
    see: ["strlen", "array_keys"],
  },
  sizeof: {
    params: [{
      n: "v",
      t: "array|string",
      d: "array or string"
    }],
    ret: "Same as count().",
    ex: 'echo sizeof([1, 2, 3, 4]);',
    out: "4",
    see: ["count"],
  },
  strtoupper: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with every ASCII letter uppercased.",
    ex: 'echo strtoupper("holy php");',
    out: "HOLY PHP",
    see: ["strtolower", "ucfirst"],
  },
  strtolower: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with every ASCII letter lowercased.",
    ex: 'echo strtolower("Holy PHP");',
    out: "holy php",
    see: ["strtoupper", "lcfirst"],
  },
  ucfirst: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with its first character uppercased.",
    ex: 'echo ucfirst("hello world");',
    out: "Hello world",
    see: ["lcfirst", "ucwords", "strtoupper"],
  },
  lcfirst: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with its first character lowercased.",
    ex: 'echo lcfirst("Holy PHP");',
    out: "holy PHP",
    see: ["ucfirst", "strtolower"],
  },
  ucwords: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with the first letter of every word uppercased.",
    ex: 'echo ucwords("holy php language");',
    out: "Holy Php Language",
    see: ["ucfirst", "strtoupper"],
  },
  trim: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string without leading or trailing whitespace.",
    ex: 'echo "[" . trim("   hello\\n\\t ") . "]";',
    out: "[hello]",
    see: ["ltrim", "rtrim"],
  },
  ltrim: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string without leading whitespace.",
    ex: 'echo "[" . ltrim("   hello  ") . "]";',
    out: "[hello  ]",
    see: ["trim", "rtrim"],
  },
  rtrim: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string without trailing whitespace.",
    ex: 'echo "[" . rtrim("   hello  ") . "]";',
    out: "[   hello]",
    see: ["trim", "ltrim", "chop"],
  },
  chop: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "Same as rtrim().",
    ex: 'echo "[" . chop("hello\\n") . "]";',
    out: "[hello]",
    see: ["rtrim"],
  },
  strrev: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string reversed byte by byte.",
    ex: 'echo strrev("holy");',
    out: "yloh",
    see: ["str_shuffle"],
  },
  str_pad: {
    params: [{
        n: "s",
        t: "string",
        d: "input string"
      },
      {
        n: "len",
        t: "int",
        d: "target length in bytes"
      },
      {
        n: "pad",
        t: "string",
        req: false,
        d: "pad string, default \" \" (space)"
      },
    ],
    ret: "The string padded on the right with repeats of $pad up to $len bytes; unchanged when already longer.",
    ex: 'echo "[" . str_pad("7", 3, "0") . "]", "\n";\necho "[" . str_pad("id:", 6) . "]", "\n";',
    out: "[700]\n[id:   ]",
    see: ["sprintf", "substr"],
  },
  str_replace: {
    params: [{
        n: "search",
        t: "string",
        d: "substring to find"
      },
      {
        n: "replace",
        t: "string",
        d: "replacement for every occurrence"
      },
      {
        n: "subject",
        t: "string",
        d: "string to modify"
      },
    ],
    ret: "The string with every occurrence of $search replaced by $replace.",
    ex: 'echo str_replace("world", "HolyPHP", "hello world"), "\n";\necho str_replace("aa", "b", "aaaa"), "\n";',
    out: "hello HolyPHP\nbb",
    see: ["preg_replace", "substr"],
  },
  substr: {
    params: [{
        n: "s",
        t: "string",
        d: "input string"
      },
      {
        n: "start",
        t: "int",
        d: "start position (negative = from the end)"
      },
      {
        n: "len",
        t: "int",
        req: false,
        d: "maximum length; omitted = to the end"
      },
    ],
    ret: "The extracted substring.",
    ex: 'echo substr("HolyPHP", 0, 4), "\n";\necho substr("HolyPHP", 4), "\n";\necho substr("HolyPHP", -3), "\n";',
    out: "Holy\nPHP\nPHP",
    see: ["strpos", "str_replace"],
  },
  strstr: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to search in"
      },
      {
        n: "needle",
        t: "string",
        d: "string to search for"
      },
    ],
    ret: "The part of $haystack starting at the first $needle, or false when not found.",
    ex: 'echo strstr("user@mail.com", "@"), "\n";\nvar_dump(strstr("user@mail.com", "!") === false);',
    out: "@mail.com\nbool(true)",
    see: ["strpos", "str_contains"],
  },
  strchr: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to search in"
      },
      {
        n: "needle",
        t: "string",
        d: "string to search for"
      },
    ],
    ret: "Alias of strstr().",
    ex: 'echo strchr("a/b/c.txt", "/");',
    out: "/b/c.txt",
    see: ["strstr", "basename"],
  },
  strpos: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to search in"
      },
      {
        n: "needle",
        t: "string",
        d: "string to search for"
      },
    ],
    ret: "0-based byte position of the first occurrence, or false when not found. Always compare with !== false — position 0 is falsy!",
    ex: '$p = strpos("HolyPHP", "PHP");\necho $p, "\n";\nvar_dump(strpos("HolyPHP", "java") === false);',
    out: "4\nbool(true)",
    see: ["str_contains", "strstr", "substr"],
  },
  str_contains: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to search in"
      },
      {
        n: "needle",
        t: "string",
        d: "string to search for"
      },
    ],
    ret: "true when $needle occurs anywhere in $haystack.",
    ex: 'var_dump(str_contains("HolyPHP", "PHP"));\nvar_dump(str_contains("HolyPHP", "java"));',
    out: "bool(true)\nbool(false)",
    see: ["strpos", "str_starts_with", "str_ends_with"],
  },
  str_starts_with: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to test"
      },
      {
        n: "needle",
        t: "string",
        d: "required prefix"
      },
    ],
    ret: "true when $haystack begins with $needle.",
    ex: 'var_dump(str_starts_with("holy.php", "holy"));\nvar_dump(str_starts_with("holy.php", ".php"));',
    out: "bool(true)\nbool(false)",
    see: ["str_ends_with", "str_contains"],
  },
  str_ends_with: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to test"
      },
      {
        n: "needle",
        t: "string",
        d: "required suffix"
      },
    ],
    ret: "true when $haystack ends with $needle.",
    ex: 'var_dump(str_ends_with("holy.php", ".php"));\nvar_dump(str_ends_with("holy.php", "holy"));',
    out: "bool(true)\nbool(false)",
    see: ["str_starts_with", "pathinfo"],
  },
  strcmp: {
    params: [{
        n: "a",
        t: "string",
        d: "first string"
      },
      {
        n: "b",
        t: "string",
        d: "second string"
      },
    ],
    ret: "< 0 when $a sorts before $b, 0 when equal, > 0 when $a sorts after $b.",
    ex: 'echo strcmp("apple", "banana"), "\n";\necho strcmp("same", "same"), "\n";\necho strcmp("b", "a"), "\n";',
    out: "-1\n0\n1",
    see: ["strcasecmp", "strncmp", "usort"],
  },
  strncmp: {
    params: [{
        n: "a",
        t: "string",
        d: "first string"
      },
      {
        n: "b",
        t: "string",
        d: "second string"
      },
      {
        n: "n",
        t: "int",
        d: "maximum bytes to compare"
      },
    ],
    ret: "Like strcmp(), limited to the first $n bytes.",
    ex: 'echo strncmp("holyphp", "holyphp2", 7);',
    out: "0",
    see: ["strcmp", "strncasecmp"],
  },
  strcasecmp: {
    params: [{
        n: "a",
        t: "string",
        d: "first string"
      },
      {
        n: "b",
        t: "string",
        d: "second string"
      },
    ],
    ret: "Case-insensitive strcmp().",
    ex: 'echo strcasecmp("HOLY", "holy"), "\n";\necho strcasecmp("holy", "php"), "\n";',
    out: "0\n-1",
    see: ["strcmp", "strncasecmp"],
  },
  strncasecmp: {
    params: [{
        n: "a",
        t: "string",
        d: "first string"
      },
      {
        n: "b",
        t: "string",
        d: "second string"
      },
      {
        n: "n",
        t: "int",
        d: "maximum bytes to compare"
      },
    ],
    ret: "Case-insensitive strncmp().",
    ex: 'echo strncasecmp("HOLYphp", "holyPHP", 4);',
    out: "0",
    see: ["strcasecmp", "strncmp"],
  },
  implode: {
    params: [{
        n: "glue",
        t: "string",
        d: "separator between elements"
      },
      {
        n: "arr",
        t: "array",
        d: "array of strings/numbers"
      },
    ],
    ret: "All elements joined by $glue.",
    ex: 'echo implode(", ", ["a", "b", "c"]), "\n";\necho implode("-", [1, 2, 3]), "\n";',
    out: "a, b, c\n1-2-3",
    see: ["explode", "join", "http_build_query"],
  },
  join: {
    params: [{
        n: "glue",
        t: "string",
        d: "separator"
      },
      {
        n: "arr",
        t: "array",
        d: "array to join"
      },
    ],
    ret: "Alias of implode().",
    ex: 'echo join("+", ["h", "p"]);',
    out: "h+p",
    see: ["implode"],
  },
  number_format: {
    params: [{
        n: "num",
        t: "float|int",
        d: "number to format"
      },
      {
        n: "decimals",
        t: "int",
        req: false,
        d: "decimal places (default 0)"
      },
      {
        n: "dec_point",
        t: "string",
        req: false,
        d: "decimal separator (default \".\")"
      },
      {
        n: "thousands_sep",
        t: "string",
        req: false,
        d: "thousands separator (default \",\")"
      },
    ],
    ret: "The formatted number string.",
    ex: 'echo number_format(1234567.891), "\n";\necho number_format(1234567.891, 2), "\n";\necho number_format(1234567.891, 2, ",", "."), "\n";',
    out: "1,234,568\n1,234,567.89\n1.234.567,89",
    see: ["round", "sprintf"],
  },
  nl2br: {
    params: [{
      n: "s",
      t: "string",
      d: "input string with newlines"
    }],
    ret: "The string with \"<br />\" inserted before every newline (newlines kept).",
    ex: 'echo nl2br("line1\\nline2"), "\n";',
    out: "line1<br />\nline2",
    see: ["htmlspecialchars"],
  },
  wordwrap: {
    params: [{
        n: "s",
        t: "string",
        d: "input string"
      },
      {
        n: "width",
        t: "int",
        d: "target line width"
      },
      {
        n: "break",
        t: "string",
        req: false,
        d: "line break string, default \"\\n\""
      },
    ],
    ret: "The string wrapped at word boundaries.",
    ex: 'echo wordwrap("holy php makes coding holy again", 12), "\n";',
    out: "holy php\nmakes coding\nholy again",
    see: ["nl2br", "explode"],
  },
  substr_count: {
    params: [{
        n: "haystack",
        t: "string",
        d: "string to search in"
      },
      {
        n: "needle",
        t: "string",
        d: "substring to count"
      },
    ],
    ret: "How many times $needle occurs in $haystack.",
    ex: 'echo substr_count("holy holy holy", "holy");',
    out: "3",
    see: ["strpos", "preg_match_all"],
  },
  str_shuffle: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The characters in random order (not cryptographically secure).",
    ex: '$sh = str_shuffle("abcdef");\necho strlen($sh) == 6 && str_contains($sh, "a") ? "ok" : "bad";',
    out: "ok",
    see: ["shuffle", "rand"],
  },
  strip_tags: {
    params: [{
        n: "s",
        t: "string",
        d: "HTML string"
      },
      {
        n: "allowed",
        t: "string",
        req: false,
        d: "tags to keep, e.g. \"<b><i>\""
      },
    ],
    ret: "The text without HTML tags.",
    ex: 'echo strip_tags("<p>holy <b>php</b></p>");',
    out: "holy php",
    see: ["htmlspecialchars", "htmlentities"],
  },
  addslashes: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with ' \" \\ and NUL backslash-escaped.",
    ex: 'echo addslashes("it\'s \"quoted\"");',
    out: "it\\'s \\\"quoted\\\"",
    see: ["stripslashes", "mysql_escape"],
  },
  stripslashes: {
    params: [{
      n: "s",
      t: "string",
      d: "escaped string"
    }],
    ret: "The string without backslash escapes.",
    ex: 'echo stripslashes("it\\\\\'s");',
    out: "it's",
    see: ["addslashes"],
  },
  sprintf: {
    params: [{
        n: "format",
        t: "string",
        d: "format string with % placeholders"
      },
      {
        n: "...",
        t: "mixed",
        d: "values for the placeholders"
      },
    ],
    ret: "The formatted string. Specifiers: %d %s %f %x %o %b %e %c, with width, precision and \"-\" left-align.",
    ex: 'echo sprintf("%s has %d points", "Ada", 42), "\n";\necho sprintf("%.2f", 3.14159), "\n";\necho sprintf("%05d", 42), "\n";',
    out: "Ada has 42 points\n3.14\n00042",
    see: ["printf", "vsprintf", "number_format"],
  },
  printf: {
    params: [{
        n: "format",
        t: "string",
        d: "format string"
      },
      {
        n: "...",
        t: "mixed",
        d: "values"
      },
    ],
    ret: "Prints the formatted string; returns its length.",
    ex: '$n = printf("[%s]\\n", "holy");\necho $n, "\n";',
    out: "[holy]\n7",
    see: ["sprintf"],
  },
  vsprintf: {
    params: [{
        n: "format",
        t: "string",
        d: "format string"
      },
      {
        n: "args",
        t: "array",
        d: "values as array"
      },
    ],
    ret: "sprintf() with values from an array.",
    ex: 'echo vsprintf("%s=%d", ["x", 7]);',
    out: "x=7",
    see: ["sprintf"],
  },
  ord: {
    params: [{
      n: "s",
      t: "string",
      d: "string; only the first byte is used"
    }],
    ret: "Byte value (0–255) of the first character.",
    ex: 'echo ord("A"), "\n";\necho ord("a"), "\n";',
    out: "65\n97",
    see: ["chr", "bin2hex"],
  },
  chr: {
    params: [{
      n: "n",
      t: "int",
      d: "byte value 0–255"
    }],
    ret: "One-character string for that byte.",
    ex: 'echo chr(72) . chr(105), "\n";\necho chr(10) == "\\n" ? "newline" : "?", "\n";',
    out: "Hi\nnewline",
    see: ["ord", "hex2bin"],
  },
  bin2hex: {
    params: [{
      n: "s",
      t: "string",
      d: "raw bytes"
    }],
    ret: "Hex string, two characters per byte.",
    ex: 'echo bin2hex("AB");',
    out: "4142",
    see: ["hex2bin", "sha256", "md5"],
  },
  hex2bin: {
    params: [{
      n: "s",
      t: "string",
      d: "hex string (even length)"
    }],
    ret: "The decoded raw bytes.",
    ex: 'echo hex2bin("486f6c79");',
    out: "Holy",
    see: ["bin2hex"],
  },
  htmlentities: {
    params: [{
      n: "s",
      t: "string",
      d: "text to escape"
    }],
    ret: "The text with HTML-critical characters converted to entities.",
    ex: 'echo htmlentities("<b>Tom & Jerry</b>");',
    out: "&lt;b&gt;Tom &amp; Jerry&lt;/b&gt;",
    see: ["htmlspecialchars", "html_entity_decode", "strip_tags"],
  },
  htmlspecialchars: {
    params: [{
      n: "s",
      t: "string",
      d: "text to escape"
    }],
    ret: "The text with only the five HTML-critical characters (& \" ' < >) escaped.",
    ex: 'echo htmlspecialchars("<a href=\\"x\\">link</a>");',
    out: "&lt;a href=\\\"x\\\"&gt;link&lt;/a&gt;",
    see: ["htmlentities", "html_entity_decode"],
  },
  html_entity_decode: {
    params: [{
      n: "s",
      t: "string",
      d: "entity-encoded text"
    }],
    ret: "The decoded text (\"&amp;\" becomes \"&\").",
    ex: 'echo html_entity_decode("Tom &amp; Jerry &lt;3");',
    out: "Tom & Jerry <3",
    see: ["htmlentities", "htmlspecialchars"],
  },
  similar_text: {
    params: [{
        n: "a",
        t: "string",
        d: "first string"
      },
      {
        n: "b",
        t: "string",
        d: "second string"
      },
    ],
    ret: "Number of matching characters — a rough similarity measure.",
    ex: 'echo similar_text("holy", "holyphp");',
    out: "4",
    see: ["levenshtein", "soundex"],
  },
  levenshtein: {
    params: [{
        n: "a",
        t: "string",
        d: "first string"
      },
      {
        n: "b",
        t: "string",
        d: "second string"
      },
    ],
    ret: "Edit distance: minimum insertions + deletions + substitutions to turn $a into $b.",
    ex: 'echo levenshtein("holy", "holyphp"), "\n";\necho levenshtein("cat", "cut"), "\n";',
    out: "3\n1",
    see: ["similar_text", "soundex"],
  },
  soundex: {
    params: [{
      n: "s",
      t: "string",
      d: "a word"
    }],
    ret: "4-character Soundex key; words that sound alike share the key.",
    ex: 'echo soundex("Robert"), "\n";\necho soundex("Rupert"), "\n";',
    out: "R163\nR163",
    see: ["metaphone", "levenshtein"],
  },
  metaphone: {
    params: [{
      n: "s",
      t: "string",
      d: "a word"
    }],
    ret: "Metaphone key — more accurate than soundex() for English.",
    ex: 'echo metaphone("phone"), "\n";\necho metaphone("fone"), "\n";',
    out: "FN\nFN",
    see: ["soundex"],
  },
  quotemeta: {
    params: [{
      n: "s",
      t: "string",
      d: "input string"
    }],
    ret: "The string with every regex metacharacter backslash-escaped.",
    ex: 'echo quotemeta("1 + 1 = 2 (yes)");',
    out: "1 \\+ 1 = 2 \\(yes\\)",
    see: ["preg_quote", "preg_match"],
  },

  /* ============ Regex ============ */
  preg_match: {
    params: [{
        n: "pattern",
        t: "string",
        d: "/…/ pattern with optional flags i, s, m"
      },
      {
        n: "subject",
        t: "string",
        d: "string to search"
      },
      {
        n: "matches",
        t: "array",
        req: false,
        d: "out-parameter: filled with the match (index 0) and capture groups (1..n)"
      },
    ],
    ret: "1 and fills $matches on success; false on no match or invalid pattern.",
    ex: '$n = preg_match("/(\\w+)@(\\w+)/", "mail me: ada@holy.php");\necho $n, "\n";\n$m = null;\npreg_match("/(\\w+)@(\\w+)/", "mail me: ada@holy.php", $m);\necho $m[1], " ", $m[2], "\n";',
    out: "1\nada holy",
    see: ["preg_match_all", "preg_replace", "preg_split"],
  },
  preg_match_all: {
    params: [{
        n: "pattern",
        t: "string",
        d: "/…/ pattern"
      },
      {
        n: "subject",
        t: "string",
        d: "string to search"
      },
      {
        n: "matches",
        t: "array",
        req: false,
        d: "out-parameter: one array per match"
      },
    ],
    ret: "Number of matches found; fills $matches with one sub-array per match.",
    ex: '$m = null;\n$n = preg_match_all("/\\d+/", "a1 b22 c333", $m);\necho $n, " ", $m[0][0], " ", $m[0][2];',
    out: "3 1 333",
    see: ["preg_match", "substr_count"],
  },
  preg_replace: {
    params: [{
        n: "pattern",
        t: "string",
        d: "/…/ pattern"
      },
      {
        n: "replacement",
        t: "string",
        d: "replacement; $1..$n reference capture groups"
      },
      {
        n: "subject",
        t: "string",
        d: "string to modify"
      },
    ],
    ret: "The string with every match replaced.",
    ex: 'echo preg_replace("/\\s+/", "_", "holy php lang"), "\n";\necho preg_replace("/(\\w+)@(\\w+)/", "$2/$1", "ada@holy"), "\n";',
    out: "holy_php_lang\nholy/ada",
    see: ["str_replace", "preg_match"],
  },
  preg_split: {
    params: [{
        n: "pattern",
        t: "string",
        d: "/…/ pattern acting as delimiter"
      },
      {
        n: "subject",
        t: "string",
        d: "string to split"
      },
    ],
    ret: "Array of the pieces between matches.",
    ex: '$p = preg_split("/[,:]+/", "a,b:c,,d");\necho implode("|", $p);',
    out: "a|b|c|d",
    see: ["explode", "preg_match"],
  },
  preg_grep: {
    params: [{
        n: "pattern",
        t: "string",
        d: "/…/ pattern"
      },
      {
        n: "arr",
        t: "array",
        d: "array of strings"
      },
    ],
    ret: "The elements that match the pattern, re-indexed 0..n.",
    ex: '$g = preg_grep("/^h/", ["holy", "php", "hi", "there"]);\necho implode(",", $g);',
    out: "holy,hi",
    see: ["array_filter", "preg_match"],
  },

  /* ============ Arrays ============ */
  array: {
    params: [{
      n: "...",
      t: "mixed",
      d: "values or \"key\" => value pairs"
    }],
    ret: "The array built from the arguments. Prefer the [...] literal.",
    ex: '$a = array("x", "k" => 1);\necho count($a), " ", $a["k"];',
    out: "2 1",
    see: ["count"],
  },
  array_keys: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "List of all keys.",
    ex: 'echo implode(",", array_keys(["x" => 1, "y" => 2]));',
    out: "x,y",
    see: ["array_values", "array_key_exists"],
  },
  array_values: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "List of all values, freshly indexed 0..n-1.",
    ex: 'echo implode(",", array_values([5 => "a", 9 => "b"]));',
    out: "a,b",
    see: ["array_keys", "array_unique"],
  },
  array_merge: {
    params: [{
      n: "...",
      t: "array",
      d: "two or more arrays"
    }],
    ret: "One merged array: later string keys overwrite, numeric keys append.",
    ex: '$m = array_merge(["a" => 1, 0 => "x"], ["a" => 2, 0 => "y"]);\necho $m["a"], count($m);',
    out: "23",
    see: ["array_replace", "array_combine"],
  },
  array_slice: {
    params: [{
        n: "a",
        t: "array",
        d: "input array"
      },
      {
        n: "offset",
        t: "int",
        d: "start index (negative = from the end)"
      },
      {
        n: "length",
        t: "int",
        req: false,
        d: "maximum element count"
      },
    ],
    ret: "The extracted section as a new array.",
    ex: '$s = array_slice([1, 2, 3, 4, 5], 1, 3);\necho implode(",", $s), "\n";\necho implode(",", array_slice([1, 2, 3, 4], -2)), "\n";',
    out: "2,3,4\n3,4",
    see: ["array_splice", "substr"],
  },
  array_reverse: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "The elements in reverse order.",
    ex: 'echo implode(",", array_reverse([1, 2, 3]));',
    out: "3,2,1",
    see: ["rsort", "array_flip"],
  },
  array_sum: {
    params: [{
      n: "a",
      t: "array",
      d: "array of numbers"
    }],
    ret: "Sum of all values (non-numerics count as 0).",
    ex: 'echo array_sum([1, 2.5, 3]);',
    out: "6.5",
    see: ["array_product", "max", "min"],
  },
  array_product: {
    params: [{
      n: "a",
      t: "array",
      d: "array of numbers"
    }],
    ret: "Product of all values.",
    ex: 'echo array_product([2, 3, 4]);',
    out: "24",
    see: ["array_sum"],
  },
  array_unique: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "The array without duplicate values (first occurrence wins).",
    ex: 'echo implode(",", array_unique([1, 2, 2, 3, 1]));',
    out: "1,2,3",
    see: ["array_values", "array_diff"],
  },
  in_array: {
    params: [{
        n: "needle",
        t: "mixed",
        d: "value to find"
      },
      {
        n: "haystack",
        t: "array",
        d: "array to search"
      },
    ],
    ret: "true when the value occurs in the array.",
    ex: 'var_dump(in_array(2, [1, 2, 3]));\nvar_dump(in_array("x", ["a", "b"]));',
    out: "bool(true)\nbool(false)",
    see: ["array_search", "array_key_exists"],
  },
  array_key_exists: {
    params: [{
        n: "key",
        t: "string|int",
        d: "key to test"
      },
      {
        n: "a",
        t: "array",
        d: "array to test"
      },
    ],
    ret: "true when the key exists — also when its value is null (unlike isset).",
    ex: '$m = ["k" => null];\nvar_dump(array_key_exists("k", $m));\nvar_dump(isset($m["k"]));',
    out: "bool(true)\nbool(false)",
    see: ["isset", "in_array"],
  },
  isset: {
    params: [{
      n: "var",
      t: "mixed",
      d: "variable or array element"
    }],
    ret: "true when the variable is set and not null.",
    ex: '$m = ["k" => 1];\nvar_dump(isset($m["k"]));\nvar_dump(isset($m["missing"]));',
    out: "bool(true)\nbool(false)",
    see: ["unset", "array_key_exists", "empty"],
  },
  unset: {
    params: [{
      n: "var",
      t: "mixed",
      d: "array element to remove"
    }],
    ret: "Removes the element from the array.",
    ex: '$m = ["a" => 1, "b" => 2];\nunset($m["a"]);\necho count($m), " ", $m["b"];',
    out: "1 2",
    see: ["isset", "array_splice"],
  },
  range: {
    params: [{
        n: "start",
        t: "int",
        d: "first value"
      },
      {
        n: "end",
        t: "int",
        d: "last value (inclusive)"
      },
    ],
    ret: "List of integers from $start to $end; counts down when $end < $start.",
    ex: 'echo implode(",", range(1, 5)), "\n";\necho implode(",", range(3, 1)), "\n";',
    out: "1,2,3,4,5\n3,2,1",
    see: ["array_fill", "count"],
  },
  compact: {
    params: [{
      n: "...",
      t: "string",
      d: "names of local variables"
    }],
    ret: "Map variable-name => value for every existing local with that name.",
    ex: '$name = "Ada";\n$score = 99;\n$c = compact("name", "score");\necho $c["name"], " ", $c["score"];',
    out: "Ada 99",
    see: ["extract", "array_combine"],
  },
  array_column: {
    params: [{
        n: "rows",
        t: "array",
        d: "array of maps"
      },
      {
        n: "col",
        t: "string",
        d: "key to pick from every row"
      },
    ],
    ret: "List of the picked column values.",
    ex: '$rows = [["n" => "a", "s" => 1], ["n" => "b", "s" => 2]];\necho implode(",", array_column($rows, "n"));',
    out: "a,b",
    see: ["array_map", "array_combine"],
  },
  array_chunk: {
    params: [{
        n: "a",
        t: "array",
        d: "input array"
      },
      {
        n: "size",
        t: "int",
        d: "maximum chunk size"
      },
    ],
    ret: "Array of arrays, each with at most $size elements.",
    ex: '$c = array_chunk([1, 2, 3, 4, 5], 2);\necho count($c), " ", implode(",", $c[2]);',
    out: "3 5",
    see: ["array_slice"],
  },
  array_pad: {
    params: [{
        n: "a",
        t: "array",
        d: "input array"
      },
      {
        n: "size",
        t: "int",
        d: "target size (negative pads on the left)"
      },
      {
        n: "value",
        t: "mixed",
        d: "fill value"
      },
    ],
    ret: "The array grown to $size elements.",
    ex: 'echo implode(",", array_pad([1], 4, 0)), "\n";\necho implode(",", array_pad([1], -3, 9)), "\n";',
    out: "1,0,0,0\n9,9,1",
    see: ["array_fill", "str_pad"],
  },
  array_replace: {
    params: [{
      n: "...",
      t: "array",
      d: "base array, then replacement arrays"
    }],
    ret: "The base array with later arrays overwriting by key (keys preserved).",
    ex: '$r = array_replace(["a" => 1, "b" => 2], ["b" => 9, "c" => 3]);\necho $r["a"], $r["b"], $r["c"];',
    out: "193",
    see: ["array_merge", "array_diff"],
  },
  array_fill_keys: {
    params: [{
        n: "keys",
        t: "array",
        d: "list of keys"
      },
      {
        n: "value",
        t: "mixed",
        d: "value for every key"
      },
    ],
    ret: "Map with each key mapped to $value.",
    ex: '$m = array_fill_keys(["x", "y", "z"], 0);\necho $m["x"], $m["y"], $m["z"];',
    out: "000",
    see: ["array_fill", "array_combine"],
  },
  array_key_first: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "The first key, or null when empty.",
    ex: 'echo array_key_first(["a" => 1, "b" => 2]), "\n";\necho array_key_first([]) == null ? "null" : "?", "\n";',
    out: "a\nnull",
    see: ["array_key_last", "reset"],
  },
  array_key_last: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "The last key, or null when empty.",
    ex: 'echo array_key_last(["a" => 1, "b" => 2]);',
    out: "b",
    see: ["array_key_first", "end"],
  },
  usort: {
    params: [{
        n: "a",
        t: "array",
        d: "array to sort (in place)"
      },
      {
        n: "cmp",
        t: "closure",
        d: "comparator fn($x, $y) returning -1/0/1 (e.g. via <=>)"
      },
    ],
    ret: "true; the array is sorted in place.",
    ex: '$a = [3, 1, 2];\nusort($a, fn($x, $y) => $x <=> $y);\necho implode(",", $a), "\n";\n\n$a2 = [["n" => "b"], ["n" => "a"]];\nusort($a2, fn($x, $y) => $x["n"] <=> $y["n"]);\necho $a2[0]["n"], "\n";',
    out: "1,2,3\na",
    see: ["sort", "uasort", "uksort"],
  },
  uasort: {
    params: [{
        n: "a",
        t: "array",
        d: "map to sort (in place)"
      },
      {
        n: "cmp",
        t: "closure",
        d: "comparator closure"
      },
    ],
    ret: "true; sorts by value while keeping key association.",
    ex: '$m = ["b" => 2, "a" => 1];\nuasort($m, fn($x, $y) => $x <=> $y);\necho array_key_first($m), " ", $m["a"];',
    out: "a 1",
    see: ["usort", "asort", "uksort"],
  },
  uksort: {
    params: [{
        n: "a",
        t: "array",
        d: "map to sort (in place)"
      },
      {
        n: "cmp",
        t: "closure",
        d: "comparator receiving two keys"
      },
    ],
    ret: "true; sorts by key using the closure.",
    ex: '$m = ["b" => 2, "a" => 1];\nuksort($m, fn($x, $y) => $x <=> $y);\necho array_key_first($m);',
    out: "a",
    see: ["usort", "ksort", "uasort"],
  },
  array_walk: {
    params: [{
        n: "a",
        t: "array",
        d: "array to walk"
      },
      {
        n: "fn",
        t: "closure",
        d: "callback fn(value, key) — receives COPIES, not references"
      },
    ],
    ret: "true; calls $fn for every element.",
    ex: '$n = 0;\narray_walk([10, 20, 30], fn($v, $k) use (&$n) => $n = $n + $v);\necho $n;',
    out: "60",
    see: ["array_map", "array_walk_recursive"],
  },
  array_walk_recursive: {
    params: [{
        n: "a",
        t: "array",
        d: "(possibly nested) array"
      },
      {
        n: "fn",
        t: "closure",
        d: "fn(&$value, $key)"
      },
    ],
    ret: "true; visits leaves of nested arrays only.",
    ex: '$a = [1, [2, 3]];\narray_walk_recursive($a, fn(&$v, $k) => $v = $v + 10);\necho $a[0], " ", $a[1][0];',
    out: "11 12",
    see: ["array_walk", "array_map"],
  },
  array_map: {
    params: [{
        n: "fn",
        t: "closure",
        d: "transform fn($value[, $value2, …])"
      },
      {
        n: "a",
        t: "array",
        d: "array to transform"
      },
      {
        n: "...",
        t: "array",
        req: false,
        d: "more arrays — one value per array is passed to $fn"
      },
    ],
    ret: "New array with the transformed values.",
    ex: '$b = array_map(fn($x) => $x * $x, [1, 2, 3, 4]);\necho implode(",", $b);',
    out: "1,4,9,16",
    see: ["array_filter", "array_reduce", "array_walk"],
  },
  array_filter: {
    params: [{
        n: "a",
        t: "array",
        d: "array to filter"
      },
      {
        n: "fn",
        t: "closure",
        req: false,
        d: "predicate; without it, truthy elements are kept"
      },
    ],
    ret: "The elements where $fn returned true (keys preserved).",
    ex: '$e = array_filter([1, 2, 3, 4], fn($x) => $x % 2 == 0);\necho implode(",", $e), "\n";\necho implode(",", array_filter([0, 1, "", "a"])), "\n";',
    out: "2,4\n1,a",
    see: ["array_map", "preg_grep"],
  },
  array_reduce: {
    params: [{
        n: "a",
        t: "array",
        d: "array to fold"
      },
      {
        n: "fn",
        t: "closure",
        d: "fn($carry, $item) => new $carry"
      },
      {
        n: "initial",
        t: "mixed",
        req: false,
        d: "starting value"
      },
    ],
    ret: "The final carry after folding left-to-right.",
    ex: '$sum = array_reduce([1, 2, 3, 4], fn($c, $x) => $c + $x, 0);\necho $sum;',
    out: "10",
    see: ["array_map", "array_sum"],
  },
  array_flip: {
    params: [{
      n: "a",
      t: "array",
      d: "map to flip"
    }],
    ret: "Map with keys and values swapped.",
    ex: '$f = array_flip(["a" => 1, "b" => 2]);\necho $f[1], $f[2];',
    out: "ab",
    see: ["array_search", "array_combine"],
  },
  array_fill: {
    params: [{
        n: "start",
        t: "int",
        d: "first index"
      },
      {
        n: "num",
        t: "int",
        d: "element count"
      },
      {
        n: "value",
        t: "mixed",
        d: "fill value"
      },
    ],
    ret: "Array with $num copies of $value starting at $start.",
    ex: 'echo implode(",", array_fill(0, 3, "x"));',
    out: "x,x,x",
    see: ["array_fill_keys", "array_pad", "range"],
  },
  array_combine: {
    params: [{
        n: "keys",
        t: "array",
        d: "list of keys"
      },
      {
        n: "values",
        t: "array",
        d: "list of values (same length)"
      },
    ],
    ret: "Map built from the two lists.",
    ex: '$m = array_combine(["a", "b"], [1, 2]);\necho $m["a"], $m["b"];',
    out: "12",
    see: ["array_fill_keys", "array_flip"],
  },
  array_diff: {
    params: [{
      n: "...",
      t: "array",
      d: "first the base array, then arrays of values to exclude"
    }],
    ret: "Values of the first array that appear in none of the others.",
    ex: 'echo implode(",", array_diff([1, 2, 3, 4], [2, 4]));',
    out: "1,3",
    see: ["array_intersect", "array_unique"],
  },
  array_intersect: {
    params: [{
      n: "...",
      t: "array",
      d: "first the base array, then arrays to intersect with"
    }],
    ret: "Values of the first array that appear in all the others.",
    ex: 'echo implode(",", array_intersect([1, 2, 3, 4], [2, 4, 5]));',
    out: "2,4",
    see: ["array_diff", "in_array"],
  },
  array_push: {
    params: [{
        n: "a",
        t: "array",
        d: "array (by reference)"
      },
      {
        n: "...",
        t: "mixed",
        d: "values to append"
      },
    ],
    ret: "New element count. $arr[] = v does the same for one value.",
    ex: '$a = [1];\n$n = array_push($a, 2, 3);\necho $n, " ", implode(",", $a);',
    out: "3 1,2,3",
    see: ["array_pop", "array_unshift"],
  },
  array_pop: {
    params: [{
      n: "a",
      t: "array",
      d: "array (by reference)"
    }],
    ret: "The removed last element (null when empty).",
    ex: '$a = [1, 2, 3];\n$last = array_pop($a);\necho $last, " ", count($a);',
    out: "3 2",
    see: ["array_push", "array_shift"],
  },
  array_shift: {
    params: [{
      n: "a",
      t: "array",
      d: "array (by reference)"
    }],
    ret: "The removed first element; the rest is re-indexed.",
    ex: '$a = [1, 2, 3];\n$first = array_shift($a);\necho $first, " ", implode(",", $a);',
    out: "1 2,3",
    see: ["array_unshift", "array_pop"],
  },
  array_unshift: {
    params: [{
        n: "a",
        t: "array",
        d: "array (by reference)"
      },
      {
        n: "...",
        t: "mixed",
        d: "values to prepend"
      },
    ],
    ret: "New element count.",
    ex: '$a = [2, 3];\n$n = array_unshift($a, 0, 1);\necho $n, " ", implode(",", $a);',
    out: "4 0,1,2,3",
    see: ["array_shift", "array_push"],
  },
  array_splice: {
    params: [{
        n: "a",
        t: "array",
        d: "array (by reference)"
      },
      {
        n: "offset",
        t: "int",
        d: "start index"
      },
      {
        n: "length",
        t: "int",
        req: false,
        d: "how many elements to remove"
      },
      {
        n: "replacement",
        t: "array",
        req: false,
        d: "elements inserted instead"
      },
    ],
    ret: "Array of the removed elements; the input array is modified.",
    ex: '$a = [1, 2, 3, 4];\n$gone = array_splice($a, 1, 2, ["x", "y"]);\necho implode(",", $gone), " / ", implode(",", $a);',
    out: "2,3 / 1,x,y,4",
    see: ["array_slice", "unset"],
  },
  shuffle: {
    params: [{
      n: "a",
      t: "array",
      d: "array (by reference)"
    }],
    ret: "true; the elements are reordered randomly (not crypto-secure).",
    ex: '$a = [1, 2, 3, 4, 5];\nshuffle($a);\necho count($a) == 5 && array_sum($a) == 15 ? "ok" : "bad";',
    out: "ok",
    see: ["str_shuffle", "rand", "sort"],
  },
  sort: {
    params: [{
      n: "a",
      t: "array",
      d: "array to sort (by reference)"
    }],
    ret: "true; sorted ascending by value, re-indexed 0..n.",
    ex: '$a = [3, 1, 2];\nsort($a);\necho implode(",", $a);',
    out: "1,2,3",
    see: ["rsort", "usort", "asort"],
  },
  rsort: {
    params: [{
      n: "a",
      t: "array",
      d: "array to sort (by reference)"
    }],
    ret: "true; sorted descending by value.",
    ex: '$a = [3, 1, 2];\nrsort($a);\necho implode(",", $a);',
    out: "3,2,1",
    see: ["sort", "krsort"],
  },
  ksort: {
    params: [{
      n: "a",
      t: "array",
      d: "map to sort by key (by reference)"
    }],
    ret: "true; sorted ascending by key, key/value association kept.",
    ex: '$m = ["b" => 2, "a" => 1];\nksort($m);\necho implode(",", array_keys($m));',
    out: "a,b",
    see: ["krsort", "asort", "uksort"],
  },
  krsort: {
    params: [{
      n: "a",
      t: "array",
      d: "map to sort by key (by reference)"
    }],
    ret: "true; sorted descending by key.",
    ex: '$m = ["b" => 2, "a" => 1];\nkrsort($m);\necho implode(",", array_keys($m));',
    out: "b,a",
    see: ["ksort", "rsort"],
  },
  asort: {
    params: [{
      n: "a",
      t: "array",
      d: "map to sort by value (by reference)"
    }],
    ret: "true; sorted ascending by value, keys kept.",
    ex: '$m = ["x" => 3, "y" => 1];\nasort($m);\necho array_key_first($m);',
    out: "y",
    see: ["arsort", "ksort", "uasort"],
  },
  arsort: {
    params: [{
      n: "a",
      t: "array",
      d: "map to sort by value (by reference)"
    }],
    ret: "true; sorted descending by value, keys kept.",
    ex: '$m = ["x" => 1, "y" => 3];\narsort($m);\necho array_key_first($m);',
    out: "y",
    see: ["asort", "krsort"],
  },
  array_search: {
    params: [{
        n: "needle",
        t: "mixed",
        d: "value to find"
      },
      {
        n: "haystack",
        t: "array",
        d: "array to search"
      },
    ],
    ret: "The key of the first match, or false when not found.",
    ex: 'echo array_search(20, [10, 20, 30]);',
    out: "1",
    see: ["in_array", "array_flip", "array_key_exists"],
  },
  end: {
    params: [{
      n: "a",
      t: "array",
      d: "array"
    }],
    ret: "The value of the last element.",
    ex: 'echo end([7, 8, 9]);',
    out: "9",
    see: ["reset", "array_key_last"],
  },
  reset: {
    params: [{
      n: "a",
      t: "array",
      d: "array"
    }],
    ret: "The value of the first element.",
    ex: 'echo reset([7, 8, 9]);',
    out: "7",
    see: ["end", "array_key_first"],
  },
  max: {
    params: [{
      n: "...",
      t: "mixed",
      d: "numbers or a single array"
    }],
    ret: "The largest value.",
    ex: 'echo max(3, 7, 5), "\n";\necho max([2, 9, 4]), "\n";',
    out: "7\n9",
    see: ["min", "array_sum"],
  },
  min: {
    params: [{
      n: "...",
      t: "mixed",
      d: "numbers or a single array"
    }],
    ret: "The smallest value.",
    ex: 'echo min(3, 7, 5), "\n";\necho min([2, 9, -4]), "\n";',
    out: "3\n-4",
    see: ["max"],
  },
  keys: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "Alias of array_keys().",
    ex: 'echo implode(",", keys(["a" => 1, "b" => 2]));',
    out: "a,b",
    see: ["array_keys", "values"],
  },
  values: {
    params: [{
      n: "a",
      t: "array",
      d: "input array"
    }],
    ret: "Alias of array_values().",
    ex: 'echo implode(",", values(["a" => 1, "b" => 2]));',
    out: "1,2",
    see: ["array_values", "keys"],
  },

  /* ============ Math ============ */
  abs: {
    params: [{
      n: "number",
      t: "int|float",
      d: "input number"
    }],
    ret: "The absolute value; int stays int, float stays float.",
    ex: 'echo abs(-5), " ", abs(2.5), " ", abs(-1.5);',
    out: "5 2.5 1.5",
    see: ["round", "floor", "ceil"],
  },
  round: {
    params: [{
        n: "num",
        t: "float|int",
        d: "number to round"
      },
      {
        n: "precision",
        t: "int",
        req: false,
        d: "decimal places (default 0)"
      },
      {
        n: "mode",
        t: "int",
        req: false,
        d: "PHP_ROUND_HALF_UP (default), _HALF_DOWN, _HALF_EVEN, _HALF_ODD"
      },
    ],
    ret: "The rounded number.",
    ex: 'echo round(3.14159, 2), " ", round(2.5), " ", round(3.5), " ", round(2.5, 0, PHP_ROUND_HALF_EVEN);',
    out: "3.14 3 4 2",
    see: ["floor", "ceil", "number_format"],
  },
  floor: {
    params: [{
      n: "number",
      t: "float|int",
      d: "input number"
    }],
    ret: "Largest integer ≤ $number, as float.",
    ex: 'echo floor(3.9), " ", floor(-3.1);',
    out: "3 -4",
    see: ["ceil", "round", "intdiv"],
  },
  ceil: {
    params: [{
      n: "number",
      t: "float|int",
      d: "input number"
    }],
    ret: "Smallest integer ≥ $number, as float.",
    ex: 'echo ceil(3.1), " ", ceil(-3.9);',
    out: "4 -3",
    see: ["floor", "round"],
  },
  sqrt: {
    params: [{
      n: "number",
      t: "float|int",
      d: "non-negative input"
    }],
    ret: "Square root as float.",
    ex: 'echo sqrt(16), " ", round(sqrt(2), 5);',
    out: "4 1.41421",
    see: ["pow", "hypot"],
  },
  pow: {
    params: [{
        n: "base",
        t: "float|int",
        d: "base"
      },
      {
        n: "exp",
        t: "float|int",
        d: "exponent"
      },
    ],
    ret: "$base raised to $exp.",
    ex: 'echo pow(2, 10), " ", pow(2, -1), " ", pow(9, 0.5);',
    out: "1024 0.5 3",
    see: ["sqrt", "exp"],
  },
  intdiv: {
    params: [{
        n: "a",
        t: "int",
        d: "dividend"
      },
      {
        n: "b",
        t: "int",
        d: "divisor (must not be 0)"
      },
    ],
    ret: "Integer division truncated toward zero.",
    ex: 'echo intdiv(7, 2), " ", intdiv(-7, 2);',
    out: "3 -3",
    see: ["fmod", "floor"],
  },
  fmod: {
    params: [{
        n: "x",
        t: "float|int",
        d: "dividend"
      },
      {
        n: "y",
        t: "float|int",
        d: "divisor"
      },
    ],
    ret: "Floating-point remainder of $x / $y.",
    ex: 'echo fmod(7.5, 2), " ", fmod(10, 4);',
    out: "1.5 2",
    see: ["intdiv"],
  },
  sin: {
    params: [{
      n: "number",
      t: "float|int",
      d: "angle in radians"
    }],
    ret: "Sine as float.",
    ex: 'echo round(sin(0), 3), " ", round(sin(M_PI / 2), 3);',
    out: "0 1",
    see: ["cos", "tan", "deg2rad"],
  },
  cos: {
    params: [{
      n: "number",
      t: "float|int",
      d: "angle in radians"
    }],
    ret: "Cosine as float.",
    ex: 'echo round(cos(0), 3), " ", round(cos(M_PI), 3);',
    out: "1 -1",
    see: ["sin", "tan"],
  },
  tan: {
    params: [{
      n: "number",
      t: "float|int",
      d: "angle in radians"
    }],
    ret: "Tangent as float.",
    ex: 'echo round(tan(0), 3), " ", round(tan(M_PI / 4), 3);',
    out: "0 1",
    see: ["sin", "cos", "atan"],
  },
  atan: {
    params: [{
      n: "number",
      t: "float|int",
      d: "tangent value"
    }],
    ret: "Arctangent in radians.",
    ex: 'echo round(atan(1), 4), " ", round(atan(0), 3);',
    out: "0.7854 0",
    see: ["tan", "atan2", "asin"],
  },
  atan2: {
    params: [{
        n: "y",
        t: "float|int",
        d: "y coordinate"
      },
      {
        n: "x",
        t: "float|int",
        d: "x coordinate"
      },
    ],
    ret: "Angle of the point (y, x) in radians — note the argument order.",
    ex: 'echo round(atan2(1, 1), 4), " ", round(atan2(0, 1), 3);',
    out: "0.7854 0",
    see: ["atan"],
  },
  asin: {
    params: [{
      n: "number",
      t: "float|int",
      d: "value in [-1, 1]"
    }],
    ret: "Arcsine in radians.",
    ex: 'echo round(asin(1), 4), " ", round(asin(0), 3);',
    out: "1.5708 0",
    see: ["sin", "acos"],
  },
  acos: {
    params: [{
      n: "number",
      t: "float|int",
      d: "value in [-1, 1]"
    }],
    ret: "Arccosine in radians.",
    ex: 'echo round(acos(-1), 4), " ", round(acos(1), 3);',
    out: "3.1416 0",
    see: ["cos", "asin"],
  },
  log: {
    params: [{
        n: "number",
        t: "float|int",
        d: "positive input"
      },
      {
        n: "base",
        t: "float|int",
        req: false,
        d: "logarithm base (default e)"
      },
    ],
    ret: "Natural logarithm, or base-$base logarithm.",
    ex: 'echo round(log(M_E), 3), " ", round(log(8, 2), 4);',
    out: "1 3",
    see: ["log2", "log10", "exp"],
  },
  log2: {
    params: [{
      n: "number",
      t: "float|int",
      d: "positive input"
    }],
    ret: "Base-2 logarithm.",
    ex: 'echo log2(8), " ", round(log2(10), 4);',
    out: "3 3.3219",
    see: ["log", "log10"],
  },
  log10: {
    params: [{
      n: "number",
      t: "float|int",
      d: "positive input"
    }],
    ret: "Base-10 logarithm.",
    ex: 'echo log10(1000), " ", round(log10(500), 3);',
    out: "3 2.699",
    see: ["log", "log2"],
  },
  exp: {
    params: [{
      n: "number",
      t: "float|int",
      d: "exponent"
    }],
    ret: "e raised to $number.",
    ex: 'echo round(exp(1), 4), " ", exp(0);',
    out: "2.7183 1",
    see: ["log", "pow"],
  },
  is_nan: {
    params: [{
      n: "number",
      t: "float",
      d: "value to test"
    }],
    ret: "true for NaN.",
    ex: 'var_dump(is_nan(sqrt(-1)));\nvar_dump(is_nan(1.5));',
    out: "bool(true)\nbool(false)",
    see: ["is_finite", "is_infinite"],
  },
  is_finite: {
    params: [{
      n: "number",
      t: "float",
      d: "value to test"
    }],
    ret: "true when the value is neither infinite nor NaN.",
    ex: 'var_dump(is_finite(1.5));\nvar_dump(is_finite(log(0)));',
    out: "bool(true)\nbool(false)",
    see: ["is_nan", "is_infinite"],
  },
  is_infinite: {
    params: [{
      n: "number",
      t: "float",
      d: "value to test"
    }],
    ret: "true for +inf and -inf.",
    ex: 'var_dump(is_infinite(log(0)));\nvar_dump(is_infinite(1.5));',
    out: "bool(true)\nbool(false)",
    see: ["is_finite", "is_nan"],
  },
  pi: {
    params: [],
    ret: "π as float. The constant M_PI is equivalent.",
    ex: 'echo pi(), " ", round(pi(), 2);',
    out: "3.141592653589793 3.14",
    see: ["M_PI", "sin", "cos"],
  },
  M_PI: {
    params: [],
    ret: "The constant π as a callable fallback — prefer the constant.",
    ex: 'echo round(M_PI, 5);',
    out: "3.14159",
    see: ["pi"],
  },
  deg2rad: {
    params: [{
      n: "number",
      t: "float|int",
      d: "degrees"
    }],
    ret: "The angle in radians.",
    ex: 'echo round(deg2rad(180), 5), " ", round(deg2rad(90), 4);',
    out: "3.14159 1.5708",
    see: ["rad2deg", "sin", "cos"],
  },
  rad2deg: {
    params: [{
      n: "number",
      t: "float|int",
      d: "radians"
    }],
    ret: "The angle in degrees.",
    ex: 'echo round(rad2deg(M_PI), 1), " ", rad2deg(1.5707963267948966);',
    out: "180 90",
    see: ["deg2rad"],
  },
  hypot: {
    params: [{
        n: "x",
        t: "float|int",
        d: "first leg"
      },
      {
        n: "y",
        t: "float|int",
        d: "second leg"
      },
    ],
    ret: "sqrt(x² + y²), overflow-safe.",
    ex: 'echo hypot(3, 4), " ", hypot(5, 12);',
    out: "5 13",
    see: ["sqrt", "pow"],
  },
  lcg_value: {
    params: [],
    ret: "Pseudo-random float in [0, 1) (not crypto-secure).",
    ex: '$v = lcg_value();\necho $v >= 0 && $v < 1 ? "in-range" : "out";',
    out: "in-range",
    see: ["rand", "mt_rand", "random_int"],
  },
  rand: {
    params: [{
        n: "min",
        t: "int",
        req: false,
        d: "lower bound (inclusive)"
      },
      {
        n: "max",
        t: "int",
        req: false,
        d: "upper bound (inclusive)"
      },
    ],
    ret: "Pseudo-random integer, in the range when given (not crypto-secure).",
    ex: '$d = rand(1, 6);\necho $d >= 1 && $d <= 6 ? "dice-ok" : "bad";',
    out: "dice-ok",
    see: ["mt_rand", "srand", "random_int", "shuffle"],
  },
  mt_rand: {
    params: [{
        n: "min",
        t: "int",
        req: false,
        d: "lower bound"
      },
      {
        n: "max",
        t: "int",
        req: false,
        d: "upper bound"
      },
    ],
    ret: "Mersenne-Twister pseudo-random integer (faster than rand, still not secure).",
    ex: '$v = mt_rand(0, 100);\necho $v >= 0 && $v <= 100 ? "ok" : "bad";',
    out: "ok",
    see: ["rand", "mt_srand", "random_int"],
  },
  srand: {
    params: [{
      n: "seed",
      t: "int",
      d: "seed value"
    }],
    ret: "Seeds rand() for reproducible sequences.",
    ex: 'srand(42);\n$a = rand(0, 1000);\nsrand(42);\n$b = rand(0, 1000);\necho $a == $b ? "reproducible" : "random";',
    out: "reproducible",
    see: ["rand", "mt_srand"],
  },
  mt_srand: {
    params: [{
      n: "seed",
      t: "int",
      d: "seed value"
    }],
    ret: "Seeds mt_rand() for reproducible sequences.",
    ex: 'mt_srand(7);\n$a = mt_rand(0, 1000);\nmt_srand(7);\n$b = mt_rand(0, 1000);\necho $a == $b ? "reproducible" : "random";',
    out: "reproducible",
    see: ["mt_rand", "srand"],
  },
  random_int: {
    params: [{
        n: "min",
        t: "int",
        d: "lower bound (inclusive)"
      },
      {
        n: "max",
        t: "int",
        d: "upper bound (inclusive)"
      },
    ],
    ret: "Cryptographically secure random integer in [$min, $max].",
    ex: '$k = random_int(1000, 9999);\necho $k >= 1000 && $k <= 9999 ? "pin-ok" : "bad";',
    out: "pin-ok",
    see: ["random_bytes", "mt_rand"],
  },

  /* ============ Types & vars ============ */
  is_int: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true when the value is an integer.",
    ex: 'var_dump(is_int(5));\nvar_dump(is_int("5"));\nvar_dump(is_int(5.0));',
    out: "bool(true)\nbool(false)\nbool(false)",
    see: ["is_integer", "is_float", "intval"],
  },
  is_integer: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "Alias of is_int().",
    ex: 'var_dump(is_integer(5));',
    out: "bool(true)",
    see: ["is_int"],
  },
  is_float: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true when the value is a float.",
    ex: 'var_dump(is_float(1.5));\nvar_dump(is_float(1));',
    out: "bool(true)\nbool(false)",
    see: ["is_int", "floatval"],
  },
  is_string: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true when the value is a string.",
    ex: 'var_dump(is_string("hi"));\nvar_dump(is_string(42));',
    out: "bool(true)\nbool(false)",
    see: ["is_int", "strval"],
  },
  is_bool: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true when the value is true or false.",
    ex: 'var_dump(is_bool(true));\nvar_dump(is_bool(1));',
    out: "bool(true)\nbool(false)",
    see: ["boolval", "is_int"],
  },
  is_array: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true when the value is an array or map.",
    ex: 'var_dump(is_array([1, 2]));\nvar_dump(is_array("ab"));',
    out: "bool(true)\nbool(false)",
    see: ["count", "is_string"],
  },
  is_null: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true when the value is null.",
    ex: '$x = null;\nvar_dump(is_null($x));\nvar_dump(is_null(0));',
    out: "bool(true)\nbool(false)",
    see: ["isset", "gettype"],
  },
  is_numeric: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to test"
    }],
    ret: "true for ints, floats and numeric strings like \"42\" or \"3.5\".",
    ex: 'var_dump(is_numeric("42"));\nvar_dump(is_numeric("3.5"));\nvar_dump(is_numeric("4e2"));\nvar_dump(is_numeric("abc"));',
    out: "bool(true)\nbool(true)\nbool(true)\nbool(false)",
    see: ["intval", "floatval"],
  },
  intval: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to convert"
    }],
    ret: "The integer value: floats truncate, strings parse their leading number (0 when not numeric).",
    ex: 'echo intval("42"), " ", intval(3.99), " ", intval("12abc"), " ", intval(true);',
    out: "42 3 12 1",
    see: ["floatval", "strval", "is_numeric"],
  },
  floatval: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to convert"
    }],
    ret: "The float value.",
    ex: 'echo floatval("3.5"), " ", floatval(2);',
    out: "3.5 2",
    see: ["doubleval", "intval"],
  },
  doubleval: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to convert"
    }],
    ret: "Alias of floatval().",
    ex: 'echo doubleval("1.25");',
    out: "1.25",
    see: ["floatval"],
  },
  strval: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to convert"
    }],
    ret: "The string representation.",
    ex: 'echo strval(42) . "!", "\n";\necho strval(1.5), "\n";',
    out: "42!\n1.5",
    see: ["intval", "sprintf"],
  },
  boolval: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to convert"
    }],
    ret: "PHP truthiness: 0, \"\", \"0\", [] and null are false; everything else true.",
    ex: 'var_dump(boolval(1));\nvar_dump(boolval(""));\nvar_dump(boolval("0"));\nvar_dump(boolval("a"));',
    out: "bool(true)\nbool(false)\nbool(false)\nbool(true)",
    see: ["isset", "is_bool"],
  },
  gettype: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to inspect"
    }],
    ret: "Type name string: \"int\", \"float\", \"string\", \"bool\", \"array\", \"null\".",
    ex: 'echo gettype(1), " ", gettype(1.5), " ", gettype("x"), " ", gettype(true), " ", gettype([]), " ", gettype(null);',
    out: "int float string bool array null",
    see: ["is_int", "var_dump", "type_of"],
  },
  var_dump: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to dump (one per call)"
    }],
    ret: "Prints type and value, recursively for arrays; returns nothing. The main debugging tool.",
    ex: 'var_dump([1, "a" => true]);\nvar_dump(42);\nvar_dump("hi");',
    out: "array(2) {\n  [0] => int(1)\n  [\"a\"] => bool(true)\n}\nint(42)\nstring(2) \"hi\"",
    see: ["print_r", "var_export", "gettype"],
  },
  print_r: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to render"
    }],
    ret: "Human-readable rendering of the value. Prints it AND returns the text (unlike PHP, where a second argument is needed for that).",
    ex: 'print_r(["a" => 1, "b" => [2, 3]]);',
    out: "Array\n(\n  [a] => 1\n  [b] => Array\n  (\n    [0] => 2\n    [1] => 3\n  )\n)",
    see: ["var_dump", "var_export"],
  },
  var_export: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to export"
    }],
    ret: "String of HolyPHP code that would recreate the value.",
    ex: 'echo var_export([1, 2]);',
    out: "array(1, 2)",
    see: ["var_dump", "print_r", "serialize"],
  },
  serialize: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to serialize"
    }],
    ret: "Storable string in HolyPHP's own format (not compatible with PHP's serialize).",
    ex: '$s = serialize(["a" => 1, "b" => "x"]);\necho strlen($s) > 0 ? "serialized" : "empty";',
    out: "serialized",
    see: ["unserialize", "json_encode"],
  },
  unserialize: {
    params: [{
      n: "s",
      t: "string",
      d: "string from serialize()"
    }],
    ret: "The restored value. Never unserialize untrusted input.",
    ex: '$a = [1, "k" => true];\n$b = unserialize(serialize($a));\necho $b["k"] ? "roundtrip-ok" : "bad";',
    out: "roundtrip-ok",
    see: ["serialize"],
  },
  json_encode: {
    params: [{
        n: "v",
        t: "mixed",
        d: "value to encode"
      },
      {
        n: "flags",
        t: "int",
        req: false,
        d: "JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_NUMERIC_CHECK | JSON_FORCE_OBJECT"
      },
    ],
    ret: "JSON string.",
    ex: 'echo json_encode(["name" => "holy", "n" => 42, "ok" => true]), "\n";\necho json_encode(["b" => [1, 2]], JSON_PRETTY_PRINT), "\n";',
    out: '{"name":"holy","n":42,"ok":true}\n{\n    "b": [\n        1,\n        2\n    ]\n}',
    see: ["json_decode", "serialize"],
  },
  json_encode_pretty: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to encode"
    }],
    ret: "json_encode() with JSON_PRETTY_PRINT pre-applied.",
    ex: 'echo json_encode_pretty(["x" => 1]), "\n";',
    out: '{\n    "x": 1\n}',
    see: ["json_encode"],
  },
  json_decode: {
    params: [{
        n: "s",
        t: "string",
        d: "JSON text"
      },
      {
        n: "assoc",
        t: "bool",
        req: false,
        d: "accepted for PHP compatibility; maps come as maps either way"
      },
    ],
    ret: "The decoded value (maps as maps, scalars as scalars); null on invalid JSON.",
    ex: '$d = json_decode(\'{"n": 42, "tags": ["a", "b"]}\');\necho $d["n"], " ", count($d["tags"]), " ", $d["tags"][0];',
    out: "42 2 a",
    see: ["json_encode"],
  },
  define: {
    params: [{
        n: "name",
        t: "string",
        d: "constant name"
      },
      {
        n: "value",
        t: "mixed",
        d: "constant value"
      },
    ],
    ret: "Declares a runtime constant.",
    ex: 'define("APP_NAME", "HolyPHP");\necho APP_NAME;',
    out: "HolyPHP",
    see: ["defined", "constant"],
  },
  defined: {
    params: [{
      n: "name",
      t: "string",
      d: "constant name"
    }],
    ret: "true when the constant exists.",
    ex: 'define("MAX_USERS", 100);\nvar_dump(defined("MAX_USERS"));\nvar_dump(defined("NOPE"));',
    out: "bool(true)\nbool(false)",
    see: ["define", "constant"],
  },
  constant: {
    params: [{
      n: "name",
      t: "string",
      d: "constant name"
    }],
    ret: "The value of the named constant.",
    ex: 'define("LIMIT", 5);\necho constant("LIMIT");',
    out: "5",
    see: ["define", "defined"],
  },
  call_user_func: {
    params: [{
        n: "fn",
        t: "closure",
        d: "callable"
      },
      {
        n: "...",
        t: "mixed",
        req: false,
        d: "arguments"
      },
    ],
    ret: "The result of the call. Direct calls are preferred.",
    ex: '$double = fn($x) => $x * 2;\necho call_user_func($double, 21);',
    out: "42",
    see: ["array_map", "usort"],
  },
  func_get_args: {
    params: [],
    ret: "Array of the arguments the current function actually received.",
    ex: 'function f($a, $b) { return count(func_get_args()); }\necho f(1, 2, 3);',
    out: "3",
    see: ["func_num_args"],
  },
  func_num_args: {
    params: [],
    ret: "Number of arguments the current function received.",
    ex: 'function f($a, $b) { return func_num_args(); }\necho f(1, 2, 3);',
    out: "3",
    see: ["func_get_args"],
  },
  type_of: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to inspect"
    }],
    ret: "Type name of the value (same names as gettype).",
    ex: 'echo type_of(42), " ", type_of("x");',
    out: "int string",
    see: ["gettype", "typeof"],
  },
  typeof: {
    params: [{
      n: "v",
      t: "mixed",
      d: "value to inspect"
    }],
    ret: "Alias of type_of().",
    ex: 'echo typeof(1.5);',
    out: "float",
    see: ["type_of"],
  },

  /* ============ Time ============ */
  time: {
    params: [],
    ret: "Current Unix timestamp (int seconds since 1970-01-01 UTC).",
    ex: '$t = time();\necho $t > 1700000000 ? "clock-ok" : "clock-suspicious";',
    out: "clock-ok",
    see: ["date", "microtime", "strtotime"],
  },
  microtime: {
    params: [],
    ret: "Current time as float seconds with microsecond precision.",
    ex: '$t0 = microtime();\n$acc = 0;\nfor ($i = 0; $i < 100000; $i++) { $acc += $i; }\necho microtime() - $t0 >= 0 ? "measured" : "clock-broke";',
    out: "measured",
    see: ["hrtime", "time"],
  },
  hrtime: {
    params: [],
    ret: "Monotonic high-resolution counter in nanoseconds — for benchmarking.",
    ex: '$t0 = hrtime();\n$acc = 0;\nfor ($i = 0; $i < 1000000; $i++) { $acc += $i; }\n$ns = hrtime() - $t0;\necho $ns > 0 ? "ns-ok" : "bad";',
    out: "ns-ok",
    see: ["microtime", "time"],
  },
  sleep: {
    params: [{
      n: "seconds",
      t: "int",
      d: "how long to pause"
    }],
    ret: "Returns after the pause.",
    ex: '$t0 = time();\nsleep(1);\necho time() - $t0 >= 1 ? "slept" : "instant";',
    out: "slept",
    see: ["usleep", "sleep_ms"],
  },
  usleep: {
    params: [{
      n: "microseconds",
      t: "int",
      d: "how long to pause"
    }],
    ret: "Returns after the pause.",
    ex: 'usleep(1000);\necho "back";',
    out: "back",
    see: ["sleep", "sleep_ms"],
  },
  sleep_ms: {
    params: [{
      n: "ms",
      t: "int",
      d: "how long to pause in milliseconds"
    }],
    ret: "Returns after the pause (HolyPHP convenience).",
    ex: '$t0 = hrtime();\nsleep_ms(50);\necho hrtime() - $t0 >= 50000000 ? "slept" : "fast";',
    out: "slept",
    see: ["sleep", "usleep"],
  },
  date: {
    params: [{
        n: "format",
        t: "string",
        d: "format letters: Y y m n d j H G i s a A D l M N W t L U"
      },
      {
        n: "timestamp",
        t: "int",
        req: false,
        d: "Unix timestamp (default: now)"
      },
    ],
    ret: "The formatted date/time string (local time).",
    ex: 'echo date("Y-m-d", strtotime("2024-03-15 12:30:00")), "\n";\necho date("H:i:s", strtotime("2024-03-15 12:30:00")), "\n";\necho date("Y-m-d H:i:s", 1710508200), "\n";',
    out: "2024-03-15\n12:30:00\n2024-03-15 12:30:00",
    see: ["strtotime", "mktime", "gmdate", "checkdate"],
  },
  mktime: {
    params: [{
        n: "hour",
        t: "int",
        d: "0–23"
      },
      {
        n: "minute",
        t: "int",
        d: "0–59"
      },
      {
        n: "second",
        t: "int",
        d: "0–59"
      },
      {
        n: "month",
        t: "int",
        d: "1–12"
      },
      {
        n: "day",
        t: "int",
        d: "1–31"
      },
      {
        n: "year",
        t: "int",
        d: "4-digit year"
      },
    ],
    ret: "Unix timestamp for that local date/time.",
    ex: '$t = mktime(12, 30, 0, 3, 15, 2024);\necho date("Y-m-d H:i:s", $t);',
    out: "2024-03-15 12:30:00",
    see: ["date", "strtotime", "checkdate"],
  },
  strtotime: {
    params: [{
      n: "s",
      t: "string",
      d: "date/time string: \"YYYY-MM-DD HH:MM:SS\", \"YYYY-MM-DD\", \"HH:MM:SS\""
    }],
    ret: "Unix timestamp, or 0 when unparseable.",
    ex: 'echo date("Y-m-d", strtotime("2024-12-24")), "\n";\necho strtotime("2024-03-15 12:30:00") > 0 ? "parsed" : "failed", "\n";',
    out: "2024-12-24\nparsed",
    see: ["date", "mktime"],
  },
  checkdate: {
    params: [{
        n: "month",
        t: "int",
        d: "1–12"
      },
      {
        n: "day",
        t: "int",
        d: "1–31"
      },
      {
        n: "year",
        t: "int",
        d: "4-digit year"
      },
    ],
    ret: "true when the date exists (knows leap years).",
    ex: 'var_dump(checkdate(2, 29, 2024));\nvar_dump(checkdate(2, 29, 2023));\nvar_dump(checkdate(13, 1, 2024));',
    out: "bool(true)\nbool(false)\nbool(false)",
    see: ["mktime", "date"],
  },
  strftime: {
    params: [{
        n: "format",
        t: "string",
        d: "strftime format string"
      },
      {
        n: "timestamp",
        t: "int",
        req: false,
        d: "Unix timestamp (default: now)"
      },
    ],
    ret: "Formatted date string (locale-style subset).",
    ex: 'echo strftime("%Y-%m-%d", strtotime("2024-03-15"));',
    out: "2024-03-15",
    see: ["date"],
  },
  getdate: {
    params: [{
      n: "timestamp",
      t: "int",
      req: false,
      d: "Unix timestamp (default: now)"
    }],
    ret: "Map with year, mon, mday, hours, minutes, seconds, wday, yday.",
    ex: '$d = getdate(strtotime("2024-03-15 12:30:00"));\necho $d["year"], "-", $d["mon"], "-", $d["mday"], " ", $d["hours"], ":", $d["minutes"];',
    out: "2024-3-15 12:30",
    see: ["date", "localtime"],
  },
  localtime: {
    params: [{
      n: "timestamp",
      t: "int",
      req: false,
      d: "Unix timestamp (default: now)"
    }],
    ret: "Numeric time-struct array (seconds, minutes, hours, …).",
    ex: '$l = localtime(strtotime("2024-03-15 12:30:00"));\necho $l[2], ":", $l[1];',
    out: "12:30",
    see: ["getdate"],
  },
  gmdate: {
    params: [{
        n: "format",
        t: "string",
        d: "date() format string"
      },
      {
        n: "timestamp",
        t: "int",
        req: false,
        d: "Unix timestamp (default: now)"
      },
    ],
    ret: "Like date() but always formatted in UTC.",
    ex: 'echo gmdate("Y-m-d", strtotime("2024-03-15 12:30:00"));',
    out: "2024-03-15",
    see: ["date"],
  },
  exit: {
    params: [{
      n: "code",
      t: "int",
      req: false,
      d: "process exit code (default 0)"
    }],
    ret: "Terminates the program immediately.",
    ex: 'echo "before\\n";\nif (true) { exit(0); }\necho "unreachable";',
    out: "before",
    see: ["die"],
  },
  die: {
    params: [{
      n: "code",
      t: "int",
      req: false,
      d: "process exit code (default 0)"
    }],
    ret: "Alias of exit().",
    ex: 'echo "bye\\n";\ndie(0);\necho "unreachable";',
    out: "bye",
    see: ["exit"],
  },

  /* ============ System & I/O ============ */
  file_get_contents: {
    params: [{
      n: "path",
      t: "string",
      d: "file path"
    }],
    ret: "The whole file as a string; throws when the file cannot be opened.",
    ex: 'file_put_contents("/tmp/hphp_doc_demo.txt", "holy contents");\necho file_get_contents("/tmp/hphp_doc_demo.txt");',
    out: "holy contents",
    see: ["file_put_contents", "fopen", "fread"],
  },
  file_put_contents: {
    params: [{
        n: "path",
        t: "string",
        d: "file path"
      },
      {
        n: "data",
        t: "string",
        d: "content to write"
      },
      {
        n: "flags",
        t: "int",
        req: false,
        d: "PHP_FILE_APPEND to append instead of truncating"
      },
    ],
    ret: "Number of bytes written.",
    ex: '$n = file_put_contents("/tmp/hphp_doc_demo2.txt", "line1\\n");\nfile_put_contents("/tmp/hphp_doc_demo2.txt", "line2", PHP_FILE_APPEND);\necho $n, "\\n", file_get_contents("/tmp/hphp_doc_demo2.txt"), "\n";',
    out: "6\nline1\nline2",
    see: ["file_get_contents", "fopen", "fwrite"],
  },
  file_exists: {
    params: [{
      n: "path",
      t: "string",
      d: "file or directory path"
    }],
    ret: "true when the path exists.",
    ex: 'file_put_contents("/tmp/hphp_doc_e.txt", "x");\nvar_dump(file_exists("/tmp/hphp_doc_e.txt"));\nvar_dump(file_exists("/tmp/hphp_doc_missing.xyz"));\nunlink("/tmp/hphp_doc_e.txt");',
    out: "bool(true)\nbool(false)",
    see: ["is_file", "is_dir", "filesize"],
  },
  is_dir: {
    params: [{
      n: "path",
      t: "string",
      d: "path to test"
    }],
    ret: "true when the path is an existing directory.",
    ex: 'mkdir("/tmp/hphp_doc_dir");\nvar_dump(is_dir("/tmp/hphp_doc_dir"));\nvar_dump(is_dir("/tmp/hphp_doc_dir/missing"));\nrmdir("/tmp/hphp_doc_dir");',
    out: "bool(true)\nbool(false)",
    see: ["is_file", "mkdir", "file_exists"],
  },
  is_file: {
    params: [{
      n: "path",
      t: "string",
      d: "path to test"
    }],
    ret: "true when the path is an existing regular file.",
    ex: 'file_put_contents("/tmp/hphp_doc_f.txt", "x");\nvar_dump(is_file("/tmp/hphp_doc_f.txt"));\nvar_dump(is_file("/tmp"));\nunlink("/tmp/hphp_doc_f.txt");',
    out: "bool(true)\nbool(false)",
    see: ["is_dir", "file_exists"],
  },
  is_readable: {
    params: [{
      n: "path",
      t: "string",
      d: "path to test"
    }],
    ret: "true when the file exists and can be read.",
    ex: 'file_put_contents("/tmp/hphp_doc_r.txt", "x");\nvar_dump(is_readable("/tmp/hphp_doc_r.txt"));\nunlink("/tmp/hphp_doc_r.txt");',
    out: "bool(true)",
    see: ["is_writable", "file_exists"],
  },
  is_writable: {
    params: [{
      n: "path",
      t: "string",
      d: "path to test"
    }],
    ret: "true when the file exists and can be written.",
    ex: 'file_put_contents("/tmp/hphp_doc_w.txt", "x");\nvar_dump(is_writable("/tmp/hphp_doc_w.txt"));\nunlink("/tmp/hphp_doc_w.txt");',
    out: "bool(true)",
    see: ["is_readable", "file_exists"],
  },
  filesize: {
    params: [{
      n: "path",
      t: "string",
      d: "file path"
    }],
    ret: "File size in bytes.",
    ex: 'file_put_contents("/tmp/hphp_doc_s.txt", "12345");\necho filesize("/tmp/hphp_doc_s.txt");\nunlink("/tmp/hphp_doc_s.txt");',
    out: "5",
    see: ["file_exists", "file_get_contents"],
  },
  fopen: {
    params: [{
        n: "path",
        t: "string",
        d: "file path"
      },
      {
        n: "mode",
        t: "string",
        d: "\"r\" read, \"w\" truncate+write, \"a\" append; add \"b\" for binary"
      },
    ],
    ret: "File handle for fgets/fread/fwrite/fclose, or false on failure.",
    ex: '$fh = fopen("/tmp/hphp_doc_fh.txt", "w");\nfwrite($fh, "streamed");\nfclose($fh);\necho file_get_contents("/tmp/hphp_doc_fh.txt");',
    out: "streamed",
    see: ["fclose", "fwrite", "fgets", "fread"],
  },
  fclose: {
    params: [{
      n: "handle",
      t: "resource",
      d: "handle from fopen()"
    }],
    ret: "true when closed. Always close files you wrote.",
    ex: '$fh = fopen("/tmp/hphp_doc_fc.txt", "w");\nfwrite($fh, "x");\nvar_dump(fclose($fh));\nunlink("/tmp/hphp_doc_fc.txt");',
    out: "bool(true)",
    see: ["fopen"],
  },
  fgets: {
    params: [{
      n: "handle",
      t: "resource",
      d: "handle from fopen() in read mode"
    }],
    ret: "One line including its newline; false at end of file.",
    ex: 'file_put_contents("/tmp/hphp_doc_l.txt", "one\\ntwo\\n");\n$fh = fopen("/tmp/hphp_doc_l.txt", "r");\necho rtrim(fgets($fh)), "\n";\necho rtrim(fgets($fh)), "\n";\nfclose($fh);',
    out: "one\ntwo",
    see: ["fread", "feof", "fopen"],
  },
  fread: {
    params: [{
        n: "handle",
        t: "resource",
        d: "handle from fopen()"
      },
      {
        n: "max",
        t: "int",
        d: "maximum bytes to read"
      },
    ],
    ret: "Up to $max bytes as a string.",
    ex: 'file_put_contents("/tmp/hphp_doc_fr.txt", "abcdef");\n$fh = fopen("/tmp/hphp_doc_fr.txt", "rb");\necho fread($fh, 3);\nfclose($fh);',
    out: "abc",
    see: ["fgets", "file_get_contents", "feof"],
  },
  fwrite: {
    params: [{
        n: "handle",
        t: "resource",
        d: "handle from fopen() in write mode"
      },
      {
        n: "data",
        t: "string",
        d: "data to write"
      },
    ],
    ret: "Number of bytes written.",
    ex: '$fh = fopen("/tmp/hphp_doc_fw.txt", "w");\n$n = fwrite($fh, "written");\nfclose($fh);\necho $n, " ", file_get_contents("/tmp/hphp_doc_fw.txt");',
    out: "7 written",
    see: ["fopen", "file_put_contents"],
  },
  feof: {
    params: [{
      n: "handle",
      t: "resource",
      d: "handle from fopen()"
    }],
    ret: "true after the end of file was reached.",
    ex: 'file_put_contents("/tmp/hphp_doc_feof.txt", "x");\n$fh = fopen("/tmp/hphp_doc_feof.txt", "r");\nfgets($fh);\nvar_dump(feof($fh));\nfclose($fh);',
    out: "bool(true)",
    see: ["fgets", "fread"],
  },
  fgetc: {
    params: [{
      n: "handle",
      t: "resource",
      d: "handle from fopen()"
    }],
    ret: "One character per call.",
    ex: 'file_put_contents("/tmp/hphp_doc_gc.txt", "ab");\n$fh = fopen("/tmp/hphp_doc_gc.txt", "r");\necho fgetc($fh), fgetc($fh);\nfclose($fh);',
    out: "ab",
    see: ["fgets", "fread"],
  },
  mkdir: {
    params: [{
        n: "path",
        t: "string",
        d: "directory to create"
      },
      {
        n: "recursive",
        t: "bool",
        req: false,
        d: "true = create parent directories too"
      },
    ],
    ret: "true on success.",
    ex: 'mkdir("/tmp/hphp_doc_m/deep", true);\nvar_dump(is_dir("/tmp/hphp_doc_m/deep"));\nrmdir("/tmp/hphp_doc_m/deep");\nrmdir("/tmp/hphp_doc_m");',
    out: "bool(true)",
    see: ["rmdir", "is_dir"],
  },
  rmdir: {
    params: [{
      n: "path",
      t: "string",
      d: "empty directory to remove"
    }],
    ret: "true on success.",
    ex: 'mkdir("/tmp/hphp_doc_rm");\nvar_dump(rmdir("/tmp/hphp_doc_rm"));\nvar_dump(is_dir("/tmp/hphp_doc_rm"));',
    out: "bool(true)\nbool(false)",
    see: ["mkdir", "unlink"],
  },
  unlink: {
    params: [{
      n: "path",
      t: "string",
      d: "file to delete"
    }],
    ret: "true on success.",
    ex: 'file_put_contents("/tmp/hphp_doc_u.txt", "x");\nvar_dump(unlink("/tmp/hphp_doc_u.txt"));\nvar_dump(file_exists("/tmp/hphp_doc_u.txt"));',
    out: "bool(true)\nbool(false)",
    see: ["rename", "rmdir"],
  },
  rename: {
    params: [{
        n: "from",
        t: "string",
        d: "old path"
      },
      {
        n: "to",
        t: "string",
        d: "new path"
      },
    ],
    ret: "true on success (also moves across directories).",
    ex: 'file_put_contents("/tmp/hphp_doc_rn1.txt", "x");\nrename("/tmp/hphp_doc_rn1.txt", "/tmp/hphp_doc_rn2.txt");\nvar_dump(file_exists("/tmp/hphp_doc_rn1.txt"));\nvar_dump(file_exists("/tmp/hphp_doc_rn2.txt"));\nunlink("/tmp/hphp_doc_rn2.txt");',
    out: "bool(false)\nbool(true)",
    see: ["copy", "unlink"],
  },
  copy: {
    params: [{
        n: "from",
        t: "string",
        d: "source path"
      },
      {
        n: "to",
        t: "string",
        d: "destination path"
      },
    ],
    ret: "true on success.",
    ex: 'file_put_contents("/tmp/hphp_doc_c1.txt", "data");\ncopy("/tmp/hphp_doc_c1.txt", "/tmp/hphp_doc_c2.txt");\necho file_get_contents("/tmp/hphp_doc_c2.txt");\nunlink("/tmp/hphp_doc_c1.txt");\nunlink("/tmp/hphp_doc_c2.txt");',
    out: "data",
    see: ["rename", "file_put_contents"],
  },
  touch: {
    params: [{
      n: "path",
      t: "string",
      d: "file path"
    }],
    ret: "true; creates the file when missing.",
    ex: 'touch("/tmp/hphp_doc_t.txt");\nvar_dump(file_exists("/tmp/hphp_doc_t.txt"));\nunlink("/tmp/hphp_doc_t.txt");',
    out: "bool(true)",
    see: ["file_exists", "fopen"],
  },
  dirname: {
    params: [{
      n: "path",
      t: "string",
      d: "file path"
    }],
    ret: "The directory part of the path.",
    ex: 'echo dirname("/var/www/index.hphp"), "\n";\necho dirname("script.hphp"), "\n";',
    out: "/var/www\n.",
    see: ["basename", "pathinfo", "realpath"],
  },
  basename: {
    params: [{
        n: "path",
        t: "string",
        d: "file path"
      },
      {
        n: "suffix",
        t: "string",
        req: false,
        d: "stripped from the end when present"
      },
    ],
    ret: "The file name part of the path.",
    ex: 'echo basename("/var/www/index.hphp"), "\n";\necho basename("page.html", ".html"), "\n";',
    out: "index.hphp\npage",
    see: ["dirname", "pathinfo"],
  },
  pathinfo: {
    params: [{
      n: "path",
      t: "string",
      d: "file path"
    }],
    ret: "Map with dirname, basename, extension and filename.",
    ex: '$p = pathinfo("/var/www/photo.jpg");\necho $p["extension"], " ", $p["filename"], " ", $p["dirname"];',
    out: "jpg photo /var/www",
    see: ["basename", "dirname"],
  },
  realpath: {
    params: [{
      n: "path",
      t: "string",
      d: "possibly relative path"
    }],
    ret: "Canonical absolute path, or false when it does not exist.",
    ex: '$r = realpath("tests");\necho $r !== false && is_dir($r) ? "resolved" : "missing";',
    out: "resolved",
    see: ["dirname", "getcwd", "is_dir"],
  },
  getcwd: {
    params: [],
    ret: "The process's current working directory.",
    ex: 'echo strlen(getcwd()) > 0 ? "cwd-ok" : "cwd-empty";',
    out: "cwd-ok",
    see: ["realpath", "sys_get_temp_dir"],
  },
  scandir: {
    params: [{
      n: "path",
      t: "string",
      d: "directory path"
    }],
    ret: "Array of entry names (including \".\" and \"..\").",
    ex: 'mkdir("/tmp/hphp_doc_sc");\nfile_put_contents("/tmp/hphp_doc_sc/a.txt", "1");\necho count(scandir("/tmp/hphp_doc_sc")) >= 3 ? "listed" : "empty";\nunlink("/tmp/hphp_doc_sc/a.txt");\nrmdir("/tmp/hphp_doc_sc");',
    out: "listed",
    see: ["opendir", "is_dir"],
  },
  opendir: {
    params: [{
      n: "path",
      t: "string",
      d: "directory path"
    }],
    ret: "Directory handle for readdir(), or false.",
    ex: 'mkdir("/tmp/hphp_doc_od");\nfile_put_contents("/tmp/hphp_doc_od/x.txt", "1");\n$dh = opendir("/tmp/hphp_doc_od");\nvar_dump($dh != false);\nclosedir($dh);\nunlink("/tmp/hphp_doc_od/x.txt");\nrmdir("/tmp/hphp_doc_od");',
    out: "bool(true)",
    see: ["readdir", "scandir", "closedir"],
  },
  readdir: {
    params: [{
      n: "handle",
      t: "resource",
      d: "handle from opendir()"
    }],
    ret: "Next entry name, or false when done.",
    ex: 'mkdir("/tmp/hphp_doc_rd");\nfile_put_contents("/tmp/hphp_doc_rd/f.txt", "1");\n$dh = opendir("/tmp/hphp_doc_rd");\n$names = [];\nwhile (($e = readdir($dh)) !== false) { $names[] = $e; }\necho in_array("f.txt", $names) ? "found" : "missing";\nclosedir($dh);\nunlink("/tmp/hphp_doc_rd/f.txt");\nrmdir("/tmp/hphp_doc_rd");',
    out: "found",
    see: ["opendir", "closedir", "scandir"],
  },
  exec: {
    params: [{
      n: "command",
      t: "string",
      d: "external command to run"
    }],
    ret: "Runs the command (Windows: via cmd /c); returns its last output line.",
    ex: 'echo strlen(exec("echo hello")) > 0 ? "ran" : "silent";',
    out: "ran",
    see: ["system", "shell_exec", "passthru"],
  },
  system: {
    params: [{
      n: "command",
      t: "string",
      d: "external command to run"
    }],
    ret: "Runs the command and prints its output.",
    ex: 'system("echo holy-output");',
    out: "holy-output",
    see: ["exec", "shell_exec"],
  },
  shell_exec: {
    params: [{
      n: "command",
      t: "string",
      d: "external command to run"
    }],
    ret: "Complete stdout of the command as a string.",
    ex: 'echo trim(shell_exec("echo shell-ok")) == "shell-ok" ? "captured" : "empty";',
    out: "captured",
    see: ["exec", "system"],
  },
  readline: {
    params: [{
      n: "prompt",
      t: "string",
      d: "text shown before reading"
    }],
    ret: "One line from stdin (for interactive scripts).",
    ex: 'echo "readline reads one stdin line";',
    out: "readline reads one stdin line",
    nocode: true,
    see: ["getenv"],
  },
  getenv: {
    params: [{
      n: "name",
      t: "string",
      d: "environment variable name"
    }],
    ret: "Value of the variable, empty string when unset.",
    ex: 'echo getenv("PATH") != "" ? "has-PATH" : "no-PATH";',
    out: "has-PATH",
    see: ["putenv"],
  },
  putenv: {
    params: [{
      n: "assignment",
      t: "string",
      d: "\"NAME=value\""
    }],
    ret: "true when set; visible to getenv() and child processes.",
    ex: 'putenv("HOLY_MODE=on");\necho getenv("HOLY_MODE");',
    out: "on",
    see: ["getenv"],
  },
  sys_get_temp_dir: {
    params: [],
    ret: "Path of the system's temporary directory.",
    ex: '$d = sys_get_temp_dir();\necho is_dir($d) ? "temp-dir-ok" : "bad";',
    out: "temp-dir-ok",
    see: ["getcwd", "file_put_contents"],
  },
  php_uname: {
    params: [],
    ret: "Operating system description string.",
    ex: 'echo str_contains(php_uname(), "Windows") ? "on-windows" : "on-other";',
    out: "on-windows",
    see: ["phpversion", "PHP_OS"],
  },
  phpversion: {
    params: [],
    ret: "HolyPHP runtime version string.",
    ex: 'echo strlen(phpversion()) > 0 ? "v-ok" : "v-missing";',
    out: "v-ok",
    see: ["php_sapi_name"],
  },
  php_sapi_name: {
    params: [],
    ret: "Interface name — \"cli\" for command-line builds.",
    ex: 'echo php_sapi_name() == "cli" ? "cli" : "other";',
    out: "cli",
    see: ["phpversion"],
  },
  memory_get_usage: {
    params: [],
    ret: "Bytes currently allocated by the runtime heap.",
    ex: 'echo memory_get_usage() > 0 ? "heap-live" : "heap-zero";',
    out: "heap-live",
    see: ["memory_get_peak_usage", "gc_collect_cycles"],
  },
  memory_get_peak_usage: {
    params: [],
    ret: "Peak bytes allocated so far.",
    ex: '$peak = memory_get_peak_usage();\necho $peak >= memory_get_usage() ? "peak-ok" : "peak-broke";',
    out: "peak-ok",
    see: ["memory_get_usage"],
  },
  gc_collect_cycles: {
    params: [],
    ret: "Number of objects freed by the forced GC pass.",
    ex: '$freed = gc_collect_cycles();\necho $freed >= 0 ? "gc-ok" : "gc-broke";',
    out: "gc-ok",
    see: ["memory_get_usage"],
  },
  sys_getloadavg: {
    params: [],
    ret: "Load average as array (platform-dependent, informational).",
    ex: '$l = sys_getloadavg();\necho is_array($l) ? "load-ok" : "load-broke";',
    out: "load-ok",
    see: ["hrtime"],
  },
  bindec: {
    params: [{
      n: "s",
      t: "string",
      d: "binary digits"
    }],
    ret: "The parsed integer.",
    ex: 'echo bindec("1010"), " ", bindec("11111111");',
    out: "10 255",
    see: ["decbin", "octdec", "hexdec"],
  },
  octdec: {
    params: [{
      n: "s",
      t: "string",
      d: "octal digits"
    }],
    ret: "The parsed integer.",
    ex: 'echo octdec("777"), " ", octdec("10");',
    out: "511 8",
    see: ["decoct", "bindec", "hexdec"],
  },
  hexdec: {
    params: [{
      n: "s",
      t: "string",
      d: "hex digits"
    }],
    ret: "The parsed integer.",
    ex: 'echo hexdec("ff"), " ", hexdec("10");',
    out: "255 16",
    see: ["dechex", "bindec", "octdec"],
  },
  stream_socket_server: {
    params: [{
      n: "addr",
      t: "string",
      d: "\"host:port\" to bind and listen on"
    }],
    ret: "Server handle for stream_socket_accept(), or -1 on failure.",
    ex: '$srv = stream_socket_server("127.0.0.1:0");\necho $srv > 0 ? "listening" : "failed";\nstream_close($srv);',
    out: "listening",
    see: ["stream_socket_client", "stream_socket_accept", "stream_close"],
  },
  stream_socket_client: {
    params: [{
      n: "addr",
      t: "string",
      d: "\"host:port\" to connect to"
    }],
    ret: "Socket handle for stream_send/stream_recv, or -1 on failure.",
    ex: '$srv = stream_socket_server("127.0.0.1:0");\n$c = stream_socket_client("127.0.0.1:1");\necho $srv > 0 && $c <= 0 ? "server-ok, no-peer" : "unexpected";\nstream_close($srv);',
    out: "server-ok, no-peer",
    see: ["stream_socket_server", "stream_send", "stream_recv"],
  },
  stream_socket_accept: {
    params: [{
      n: "server",
      t: "resource",
      d: "handle from stream_socket_server()"
    }],
    ret: "Connection handle for one accepted client (blocks until a client connects).",
    ex: 'echo "see registry/server.hphp for the full accept loop";',
    out: "see registry/server.hphp for the full accept loop",
    nocode: true,
    see: ["stream_socket_server", "stream_socket_accept2"],
  },
  stream_socket_accept2: {
    params: [{
        n: "server",
        t: "resource",
        d: "server handle"
      },
      {
        n: "timeout_ms",
        t: "int",
        d: "how long to wait for a client"
      },
    ],
    ret: "Connection handle, or false when the timeout expires first.",
    ex: '$srv = stream_socket_server("127.0.0.1:0");\n$r = stream_socket_accept2($srv, 50);\necho $r == false ? "timeout-ok" : "got-client";\nstream_close($srv);',
    out: "timeout-ok",
    see: ["stream_socket_accept", "stream_socket_server"],
  },
  stream_set_blocking: {
    params: [{
        n: "sock",
        t: "resource",
        d: "socket handle"
      },
      {
        n: "blocking",
        t: "bool",
        d: "true = blocking, false = non-blocking"
      },
    ],
    ret: "true when the mode was applied.",
    ex: '$srv = stream_socket_server("127.0.0.1:0");\nstream_set_blocking($srv, false);\necho "non-blocking set";\nstream_close($srv);',
    out: "non-blocking set",
    see: ["stream_recv", "stream_socket_server"],
  },
  stream_recv: {
    params: [{
        n: "sock",
        t: "resource",
        d: "socket handle"
      },
      {
        n: "max",
        t: "int",
        req: false,
        d: "maximum bytes (default 4096, max 1 MiB)"
      },
    ],
    ret: "Received bytes as a string (\"\" when nothing arrived).",
    ex: 'echo "used in registry/server.hphp and lib/websocket.hphp";',
    out: "used in registry/server.hphp and lib/websocket.hphp",
    nocode: true,
    see: ["stream_send", "stream_eof"],
  },
  stream_send: {
    params: [{
        n: "sock",
        t: "resource",
        d: "socket handle"
      },
      {
        n: "data",
        t: "string",
        d: "bytes to send"
      },
    ],
    ret: "Number of bytes sent.",
    ex: 'echo "used in registry/server.hphp and lib/websocket.hphp";',
    out: "used in registry/server.hphp and lib/websocket.hphp",
    nocode: true,
    see: ["stream_recv"],
  },
  stream_eof: {
    params: [{
      n: "sock",
      t: "resource",
      d: "socket handle"
    }],
    ret: "true when the peer closed the connection.",
    ex: 'echo "see registry/server.hphp";',
    out: "see registry/server.hphp",
    nocode: true,
    see: ["stream_recv"],
  },
  stream_peer: {
    params: [{
      n: "sock",
      t: "resource",
      d: "socket handle"
    }],
    ret: "\"IP:port\" of the remote end.",
    ex: 'echo "see registry/server.hphp";',
    out: "see registry/server.hphp",
    nocode: true,
    see: ["stream_socket_accept"],
  },
  stream_close: {
    params: [{
      n: "sock",
      t: "resource",
      d: "socket or server handle"
    }],
    ret: "true when closed.",
    ex: '$srv = stream_socket_server("127.0.0.1:0");\nvar_dump(stream_close($srv));',
    out: "bool(true)",
    see: ["stream_socket_server", "stream_socket_client"],
  },

  /* ============ HTTP & MySQL ============ */
  http_get: {
    params: [{
      n: "url",
      t: "string",
      d: "http:// URL (TLS is not supported)"
    }],
    ret: "[\"status\" => 200, \"body\" => …, \"contentType\" => …] on success, false on failure.",
    ex: '$r = http_get("http://localhost:1/unreachable");\necho $r === false ? "offline-safe" : "unexpected";',
    out: "offline-safe",
    see: ["http_post", "http_request", "parse_url"],
  },
  http_post: {
    params: [{
        n: "url",
        t: "string",
        d: "http:// URL"
      },
      {
        n: "body",
        t: "string",
        d: "request body"
      },
      {
        n: "headers",
        t: "array",
        req: false,
        d: "header lines like \"Content-Type: application/json\""
      },
    ],
    ret: "Same map as http_get(), or false on failure.",
    ex: '$r = http_post("http://localhost:1/unreachable", "a=1", ["Content-Type: application/x-www-form-urlencoded"]);\necho $r === false ? "offline-safe" : "unexpected";',
    out: "offline-safe",
    see: ["http_get", "http_build_query", "http_request"],
  },
  http_request: {
    params: [{
        n: "url",
        t: "string",
        d: "http:// URL"
      },
      {
        n: "method",
        t: "string",
        d: "HTTP verb: \"PUT\", \"DELETE\", … (default \"GET\")"
      },
      {
        n: "body",
        t: "string",
        req: false,
        d: "request body"
      },
      {
        n: "headers",
        t: "array",
        req: false,
        d: "header lines"
      },
    ],
    ret: "Response map like http_get(), or false on failure.",
    ex: '$r = http_request("http://localhost:1/unreachable", "PUT", "data");\necho $r === false ? "offline-safe" : "unexpected";',
    out: "offline-safe",
    see: ["http_get", "http_post"],
  },
  parse_url: {
    params: [{
      n: "url",
      t: "string",
      d: "URL to split"
    }],
    ret: "Map with scheme, user, host, port, path, query, fragment; missing parts are empty, port defaults to 80/443.",
    ex: '$u = parse_url("http://user@host:8080/path?query#frag");\necho $u["scheme"], " ", $u["host"], " ", $u["port"], " ", $u["path"], " ", $u["query"], " ", $u["fragment"];',
    out: "http host 8080 /path query frag",
    see: ["http_build_query", "http_get", "urlencode"],
  },
  http_build_query: {
    params: [{
      n: "data",
      t: "map",
      d: "key => value pairs"
    }],
    ret: "URL-encoded query string.",
    ex: 'echo http_build_query(["q" => "holy php", "page" => 2]);',
    out: "q=holy+php&page=2",
    see: ["parse_url", "urlencode", "http_post"],
  },
  mysql_connect: {
    params: [{
        n: "host",
        t: "string",
        d: "MySQL/MariaDB host (TCP)"
      },
      {
        n: "port",
        t: "int",
        req: false,
        d: "port (default 3306)"
      },
      {
        n: "user",
        t: "string",
        req: false,
        d: "user name"
      },
      {
        n: "pass",
        t: "string",
        req: false,
        d: "password"
      },
      {
        n: "db",
        t: "string",
        req: false,
        d: "database to select"
      },
    ],
    ret: "Connection handle for mysql_query/mysql_exec, or false on failure.",
    ex: '$c = mysql_connect("localhost", 3306, "root", "", "test");\necho $c ? "connected" : "no-server (ok offline)";\nif ($c) { mysql_close($c); }',
    out: "no-server (ok offline)",
    nocode: true,
    see: ["mysql_query", "mysql_exec", "mysql_escape"],
  },
  mysql_query: {
    params: [{
        n: "conn",
        t: "resource",
        d: "handle from mysql_connect()"
      },
      {
        n: "sql",
        t: "string",
        d: "SELECT/SHOW statement"
      },
    ],
    ret: "Rows as array of maps (column name => value), false on error.",
    ex: 'echo "rows = mysql_query($c, \"SELECT id, name FROM users\");";',
    out: "rows = mysql_query($c, \"SELECT id, name FROM users\");",
    nocode: true,
    see: ["mysql_connect", "mysql_exec", "mysql_insert_id"],
  },
  mysql_exec: {
    params: [{
        n: "conn",
        t: "resource",
        d: "connection handle"
      },
      {
        n: "sql",
        t: "string",
        d: "INSERT/UPDATE/DELETE/CREATE statement"
      },
    ],
    ret: "Number of affected rows, false on error.",
    ex: 'echo "affected = mysql_exec($c, \"DELETE FROM logs WHERE old = 1\");";',
    out: "affected = mysql_exec($c, \"DELETE FROM logs WHERE old = 1\");",
    nocode: true,
    see: ["mysql_query", "mysql_insert_id"],
  },
  mysql_insert_id: {
    params: [{
      n: "conn",
      t: "resource",
      d: "connection handle"
    }],
    ret: "AUTO_INCREMENT value of the last INSERT on this connection.",
    ex: 'echo "id = mysql_insert_id($c); // after an INSERT";',
    out: "id = mysql_insert_id($c); // after an INSERT",
    nocode: true,
    see: ["mysql_exec"],
  },
  mysql_close: {
    params: [{
      n: "conn",
      t: "resource",
      d: "connection handle"
    }],
    ret: "true when closed.",
    ex: 'echo "mysql_close($c); // release the connection";',
    out: "mysql_close($c); // release the connection",
    nocode: true,
    see: ["mysql_connect"],
  },
  mysql_escape: {
    params: [{
      n: "s",
      t: "string",
      d: "raw string going into SQL text"
    }],
    ret: "Escaped string (' \" \\ NUL) safe for SQL literals.",
    ex: 'echo mysql_escape("it\'s O\'Brien");',
    out: "it\\\\'s O\\\\'Brien",
    see: ["mysql_query", "addslashes"],
  },

  /* ============ Hash & encode ============ */
  md5: {
    params: [{
      n: "s",
      t: "string",
      d: "input data"
    }],
    ret: "32-character hex MD5 digest. Broken for security — checksums only.",
    ex: 'echo md5("holy");',
    out: "REPLACE_MD5_HOLY",
    see: ["sha1", "sha256", "crc32"],
  },
  sha1: {
    params: [{
      n: "s",
      t: "string",
      d: "input data"
    }],
    ret: "40-character hex SHA-1 digest (legacy — prefer sha256).",
    ex: 'echo sha1("holy");',
    out: "REPLACE_SHA1_HOLY",
    see: ["sha256", "md5"],
  },
  sha256: {
    params: [{
      n: "s",
      t: "string",
      d: "input data"
    }],
    ret: "64-character hex SHA-256 digest — the standard hash of this runtime.",
    ex: 'echo sha256("holy"), "\n";\necho sha256("abc"), "\n";',
    out: "1551d1eb020a113540d8ceb787d53b17750037ceeade4ceeafb67919ebbd7265\nba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    see: ["sha1", "hash_hmac", "password_hash"],
  },
  crc32: {
    params: [{
      n: "s",
      t: "string",
      d: "input data"
    }],
    ret: "CRC-32 checksum as integer — detects accidental corruption.",
    ex: '$c = crc32("holy");\necho crc32("holy") == $c ? "stable" : "unstable";',
    out: "stable",
    see: ["md5", "sha256"],
  },
  hash_hmac: {
    params: [{
        n: "algo",
        t: "string",
        d: "\"sha256\""
      },
      {
        n: "data",
        t: "string",
        d: "message to sign"
      },
      {
        n: "key",
        t: "string",
        d: "secret key"
      },
    ],
    ret: "HMAC digest as hex — proves the message came from someone holding $key.",
    ex: '$sig = hash_hmac("sha256", "order:42", "secret-key");\necho strlen($sig) == 64 ? "signed" : "bad";\necho hash_hmac("sha256", "order:42", "secret-key") == $sig ? " matches" : " nope";',
    out: "signed matches",
    see: ["sha256", "hash_equals"],
  },
  hash_equals: {
    params: [{
        n: "known",
        t: "string",
        d: "expected value"
      },
      {
        n: "user",
        t: "string",
        d: "user-supplied value"
      },
    ],
    ret: "true when equal — compared in constant time so timing attacks fail. Always use for secrets.",
    ex: 'var_dump(hash_equals("sig123", "sig123"));\nvar_dump(hash_equals("sig123", "sig124"));',
    out: "bool(true)\nbool(false)",
    see: ["hash_hmac", "password_verify"],
  },
  password_hash: {
    params: [{
      n: "password",
      t: "string",
      d: "plain password"
    }],
    ret: "Storable hash string ($hphp$4096$salt$digest, PBKDF2-SHA256). The salt is random per call.",
    ex: '$h = password_hash("correct horse");\necho strlen($h) > 40 && password_verify("correct horse", $h) ? "hash-ok" : "hash-bad";',
    out: "hash-ok",
    see: ["password_verify", "hash_equals", "random_bytes"],
  },
  password_verify: {
    params: [{
        n: "password",
        t: "string",
        d: "plain password to check"
      },
      {
        n: "hash",
        t: "string",
        d: "hash from password_hash()"
      },
    ],
    ret: "true when the password matches the hash (timing-safe).",
    ex: '$h = password_hash("secret");\nvar_dump(password_verify("secret", $h));\nvar_dump(password_verify("Secret", $h));',
    out: "bool(true)\nbool(false)",
    see: ["password_hash"],
  },
  base64_encode: {
    params: [{
      n: "s",
      t: "string",
      d: "raw bytes"
    }],
    ret: "Base64 text (with = padding).",
    ex: 'echo base64_encode("holy php");',
    out: "aG9seSBwaHA=",
    see: ["base64_decode", "bin2hex"],
  },
  base64_decode: {
    params: [{
      n: "s",
      t: "string",
      d: "Base64 text"
    }],
    ret: "The decoded raw bytes; invalid characters are skipped.",
    ex: 'echo base64_decode("aG9seSBwaHA=");',
    out: "holy php",
    see: ["base64_encode"],
  },
  urlencode: {
    params: [{
      n: "s",
      t: "string",
      d: "text for a query string"
    }],
    ret: "Encoded text: space becomes +, reserved characters become %XX.",
    ex: 'echo urlencode("holy php & more");',
    out: "holy+php+%26+more",
    see: ["urldecode", "http_build_query", "rawurlencode"],
  },
  urldecode: {
    params: [{
      n: "s",
      t: "string",
      d: "encoded text"
    }],
    ret: "Decoded text.",
    ex: 'echo urldecode("holy+php+%26+more");',
    out: "holy php & more",
    see: ["urlencode"],
  },
  rawurlencode: {
    params: [{
      n: "s",
      t: "string",
      d: "text for a URL path segment"
    }],
    ret: "Encoded like urlencode() but space becomes %20 (RFC 3986).",
    ex: 'echo rawurlencode("holy php");',
    out: "holy%20php",
    see: ["rawurldecode", "urlencode"],
  },
  rawurldecode: {
    params: [{
      n: "s",
      t: "string",
      d: "encoded text"
    }],
    ret: "Decoded text.",
    ex: 'echo rawurldecode("holy%20php");',
    out: "holy php",
    see: ["rawurlencode"],
  },
  random_bytes: {
    params: [{
      n: "n",
      t: "int",
      d: "byte count (1–1024)"
    }],
    ret: "$n cryptographically secure random bytes as a binary string.",
    ex: '$tok = bin2hex(random_bytes(16));\necho strlen($tok) == 32 ? "token-ok" : "token-bad";',
    out: "token-ok",
    see: ["random_int", "bin2hex", "password_hash"],
  },

  /* ============ UI ============ */
  ui_run_main: {
    params: [],
    ret: "Enters the UI event loop; blocks until the last window closes or ui_quit() is called.",
    ex: 'echo "place after building windows: ui_run_main();";',
    out: "place after building windows: ui_run_main();",
    nocode: true,
    see: ["ui_window", "ui_quit", "ui_alive"],
  },
  ui_quit: {
    params: [],
    ret: "Ends the event loop and closes the application.",
    ex: 'echo "ui_quit(); // ends the app";',
    out: "ui_quit(); // ends the app",
    nocode: true,
    see: ["ui_run_main"],
  },
  ui_window: {
    params: [{
        n: "title",
        t: "string",
        d: "window title"
      },
      {
        n: "x",
        t: "int",
        d: "left position"
      },
      {
        n: "y",
        t: "int",
        d: "top position"
      },
      {
        n: "w",
        t: "int",
        d: "width"
      },
      {
        n: "h",
        t: "int",
        d: "height"
      },
    ],
    ret: "Window handle for ui_show/ui_add/ui_title, …",
    ex: 'echo "$win = ui_window(\"HolyPHP\", 100, 100, 400, 300);";',
    out: "$win = ui_window(\"HolyPHP\", 100, 100, 400, 300);",
    nocode: true,
    see: ["ui_run_main", "ui_add", "ui_show"],
  },
  ui_show: {
    params: [{
        n: "handle",
        t: "int",
        d: "window/control handle"
      },
      {
        n: "visible",
        t: "bool",
        d: "true = show, false = hide"
      },
    ],
    ret: "Shows or hides the window/control.",
    ex: 'echo "ui_show($win, true);";',
    out: "ui_show($win, true);",
    nocode: true,
    see: ["ui_window"],
  },
  ui_title: {
    params: [{
        n: "handle",
        t: "int",
        d: "window handle"
      },
      {
        n: "title",
        t: "string",
        d: "new title"
      },
    ],
    ret: "Sets the window title.",
    ex: 'echo "ui_title($win, \"New title\");";',
    out: "ui_title($win, \"New title\");",
    nocode: true,
    see: ["ui_window"],
  },
  ui_size: {
    params: [{
        n: "handle",
        t: "int",
        d: "window/control handle"
      },
      {
        n: "w",
        t: "int",
        d: "width"
      },
      {
        n: "h",
        t: "int",
        d: "height"
      },
    ],
    ret: "Resizes the window/control.",
    ex: 'echo "ui_size($win, 600, 400);";',
    out: "ui_size($win, 600, 400);",
    nocode: true,
    see: ["ui_pos", "ui_window"],
  },
  ui_pos: {
    params: [{
        n: "handle",
        t: "int",
        d: "window/control handle"
      },
      {
        n: "x",
        t: "int",
        d: "left"
      },
      {
        n: "y",
        t: "int",
        d: "top"
      },
    ],
    ret: "Moves the window/control.",
    ex: 'echo "ui_pos($win, 200, 150);";',
    out: "ui_pos($win, 200, 150);",
    nocode: true,
    see: ["ui_size"],
  },
  ui_close: {
    params: [{
      n: "handle",
      t: "int",
      d: "window/control handle"
    }],
    ret: "Destroys the window/control.",
    ex: 'echo "ui_close($ctrl);";',
    out: "ui_close($ctrl);",
    nocode: true,
    see: ["ui_window"],
  },
  ui_alive: {
    params: [],
    ret: "true while the event loop is still running.",
    ex: 'echo "while (ui_alive()) { /* polling alternative */ }";',
    out: "while (ui_alive()) { /* polling alternative */ }",
    nocode: true,
    see: ["ui_run_main", "ui_quit"],
  },
  ui_add: {
    params: [{
        n: "parent",
        t: "int",
        d: "window or container handle"
      },
      {
        n: "kind",
        t: "int",
        d: "1 window 2 button 3 label 4 input 5 checkbox 6 list 7 combo 8 progress 9 slider 10 group 11 tab 12 panel 13 menu 14 menuitem 15 picture"
      },
      {
        n: "text",
        t: "string",
        d: "label / initial text"
      },
      {
        n: "x",
        t: "int",
        d: "left inside parent"
      },
      {
        n: "y",
        t: "int",
        d: "top inside parent"
      },
      {
        n: "w",
        t: "int",
        d: "width"
      },
      {
        n: "h",
        t: "int",
        d: "height"
      },
    ],
    ret: "Control handle for ui_set/ui_get/ui_on, …",
    ex: 'echo "$btn = ui_add($win, 2, \"Click me\", 20, 20, 120, 30); // 2 = button";',
    out: "$btn = ui_add($win, 2, \"Click me\", 20, 20, 120, 30); // 2 = button",
    nocode: true,
    see: ["ui_on", "ui_set", "ui_get"],
  },
  ui_set: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "text",
        t: "string",
        d: "new text/value"
      },
    ],
    ret: "Sets the control's text (label, button caption, input value, …).",
    ex: 'echo "ui_set($lbl, \"Count: \" . $n);";',
    out: "ui_set($lbl, \"Count: \" . $n);",
    nocode: true,
    see: ["ui_get"],
  },
  ui_get: {
    params: [{
      n: "handle",
      t: "int",
      d: "control handle"
    }],
    ret: "Current text/value of the control.",
    ex: 'echo "$name = ui_get($input);";',
    out: "$name = ui_get($input);",
    nocode: true,
    see: ["ui_set"],
  },
  ui_enable: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "enabled",
        t: "bool",
        d: "true = enable, false = disable (grayed out)"
      },
    ],
    ret: "Enables or disables the control.",
    ex: 'echo "ui_enable($btn, false); // gray out";',
    out: "ui_enable($btn, false); // gray out",
    nocode: true,
    see: ["ui_ctrl_show"],
  },
  ui_move: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "x",
        t: "int",
        d: "left inside parent"
      },
      {
        n: "y",
        t: "int",
        d: "top inside parent"
      },
    ],
    ret: "Moves the control inside its parent.",
    ex: 'echo "ui_move($btn, 40, 60);";',
    out: "ui_move($btn, 40, 60);",
    nocode: true,
    see: ["ui_pos", "ui_size"],
  },
  ui_focus: {
    params: [{
      n: "handle",
      t: "int",
      d: "control handle"
    }],
    ret: "Gives the control the keyboard focus.",
    ex: 'echo "ui_focus($input);";',
    out: "ui_focus($input);",
    nocode: true,
    see: ["ui_add"],
  },
  ui_items: {
    params: [{
      n: "handle",
      t: "int",
      d: "list/combo handle"
    }],
    ret: "Number of items in the list/combo.",
    ex: 'echo "count: " . ui_items($list) . \" items\";',
    out: "count: ui_items($list) items",
    nocode: true,
    see: ["ui_add", "ui_selected", "ui_item_text"],
  },
  ui_remove: {
    params: [{
        n: "handle",
        t: "int",
        d: "list/combo handle"
      },
      {
        n: "index",
        t: "int",
        d: "item index to remove"
      },
    ],
    ret: "Removes one item.",
    ex: 'echo "ui_remove($list, 0);";',
    out: "ui_remove($list, 0);",
    nocode: true,
    see: ["ui_clear", "ui_items"],
  },
  ui_clear: {
    params: [{
      n: "handle",
      t: "int",
      d: "list/combo/input handle"
    }],
    ret: "Removes all items (or all text).",
    ex: 'echo "ui_clear($list);";',
    out: "ui_clear($list);",
    nocode: true,
    see: ["ui_remove"],
  },
  ui_ctrl_show: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "visible",
        t: "bool",
        d: "true = show"
      },
    ],
    ret: "Shows or hides one control.",
    ex: 'echo "ui_ctrl_show($panel, false);";',
    out: "ui_ctrl_show($panel, false);",
    nocode: true,
    see: ["ui_enable", "ui_show"],
  },
  ui_selected: {
    params: [{
      n: "handle",
      t: "int",
      d: "list/combo handle"
    }],
    ret: "Index of the selected item (-1 when none).",
    ex: 'echo "$i = ui_selected($list); // -1 = nothing";',
    out: "$i = ui_selected($list); // -1 = nothing",
    nocode: true,
    see: ["ui_select", "ui_item_text", "ui_items"],
  },
  ui_select: {
    params: [{
        n: "handle",
        t: "int",
        d: "list/combo handle"
      },
      {
        n: "index",
        t: "int",
        d: "item index to select"
      },
    ],
    ret: "Selects an item programmatically.",
    ex: 'echo "ui_select($list, 0);";',
    out: "ui_select($list, 0);",
    nocode: true,
    see: ["ui_selected"],
  },
  ui_item_text: {
    params: [{
        n: "handle",
        t: "int",
        d: "list/combo handle"
      },
      {
        n: "index",
        t: "int",
        d: "item index"
      },
    ],
    ret: "Text of the item at $index.",
    ex: 'echo "ui_item_text($list, ui_selected($list));";',
    out: "ui_item_text($list, ui_selected($list));",
    nocode: true,
    see: ["ui_selected", "ui_items"],
  },
  ui_check_get: {
    params: [{
      n: "handle",
      t: "int",
      d: "checkbox handle"
    }],
    ret: "true when checked.",
    ex: 'echo "if (ui_check_get($cb)) { … }";',
    out: "if (ui_check_get($cb)) { … }",
    nocode: true,
    see: ["ui_check_set", "ui_add"],
  },
  ui_check_set: {
    params: [{
        n: "handle",
        t: "int",
        d: "checkbox handle"
      },
      {
        n: "checked",
        t: "bool",
        d: "new state"
      },
    ],
    ret: "Sets the checkbox state.",
    ex: 'echo "ui_check_set($cb, true);";',
    out: "ui_check_set($cb, true);",
    nocode: true,
    see: ["ui_check_get"],
  },
  ui_progress: {
    params: [{
        n: "handle",
        t: "int",
        d: "progress-bar handle"
      },
      {
        n: "percent",
        t: "int",
        d: "0–100"
      },
    ],
    ret: "Sets the progress value.",
    ex: 'echo "ui_progress($bar, 75);";',
    out: "ui_progress($bar, 75);",
    nocode: true,
    see: ["ui_add", "ui_slider_set"],
  },
  ui_slider_get: {
    params: [{
      n: "handle",
      t: "int",
      d: "slider handle"
    }],
    ret: "Current slider value.",
    ex: 'echo "$v = ui_slider_get($sl);";',
    out: "$v = ui_slider_get($sl);",
    nocode: true,
    see: ["ui_slider_set"],
  },
  ui_slider_set: {
    params: [{
        n: "handle",
        t: "int",
        d: "slider handle"
      },
      {
        n: "value",
        t: "int",
        d: "new value"
      },
    ],
    ret: "Sets the slider value.",
    ex: 'echo "ui_slider_set($sl, 50);";',
    out: "ui_slider_set($sl, 50);",
    nocode: true,
    see: ["ui_slider_get", "ui_progress"],
  },
  ui_bg: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "color",
        t: "int",
        d: "RGB value, e.g. 0xFF0000 for red"
      },
    ],
    ret: "Sets the background color.",
    ex: 'echo "ui_bg($panel, 0x1b1c23);";',
    out: "ui_bg($panel, 0x1b1c23);",
    nocode: true,
    see: ["ui_fg", "ui_font"],
  },
  ui_fg: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "color",
        t: "int",
        d: "RGB value"
      },
    ],
    ret: "Sets the text color.",
    ex: 'echo "ui_fg($lbl, 0xFF7B3D);";',
    out: "ui_fg($lbl, 0xFF7B3D);",
    nocode: true,
    see: ["ui_bg", "ui_font"],
  },
  ui_font: {
    params: [{
        n: "handle",
        t: "int",
        d: "control handle"
      },
      {
        n: "size",
        t: "int",
        d: "font size in points"
      },
      {
        n: "bold",
        t: "bool",
        req: false,
        d: "bold text"
      },
      {
        n: "italic",
        t: "bool",
        req: false,
        d: "italic text"
      },
      {
        n: "family",
        t: "string",
        req: false,
        d: "font family name"
      },
    ],
    ret: "Sets the control's font.",
    ex: 'echo "ui_font($title, 16, true);";',
    out: "ui_font($title, 16, true);",
    nocode: true,
    see: ["ui_bg", "ui_fg"],
  },
  ui_menu: {
    params: [{
        n: "win",
        t: "int",
        d: "window handle"
      },
      {
        n: "title",
        t: "string",
        d: "menu title in the menu bar"
      },
    ],
    ret: "Menu handle for ui_menu_item/ui_menu_sep.",
    ex: 'echo "$m = ui_menu($win, \"File\");";',
    out: "$m = ui_menu($win, \"File\");",
    nocode: true,
    see: ["ui_menu_item", "ui_menu_sep", "ui_on"],
  },
  ui_menu_item: {
    params: [{
        n: "menu",
        t: "int",
        d: "menu handle"
      },
      {
        n: "title",
        t: "string",
        d: "item label"
      },
    ],
    ret: "Item handle — attach a callback with ui_on($item, \"click\", …).",
    ex: 'echo "$open = ui_menu_item($m, \"Open…\");";',
    out: "$open = ui_menu_item($m, \"Open…\");",
    nocode: true,
    see: ["ui_menu", "ui_on", "ui_menu_sep"],
  },
  ui_menu_sep: {
    params: [{
      n: "menu",
      t: "int",
      d: "menu handle"
    }],
    ret: "Adds a separator line.",
    ex: 'echo "ui_menu_sep($m);";',
    out: "ui_menu_sep($m);",
    nocode: true,
    see: ["ui_menu_item"],
  },
  ui_menu_check: {
    params: [{
        n: "handle",
        t: "int",
        d: "menu item handle"
      },
      {
        n: "checked",
        t: "bool",
        d: "checkmark state"
      },
    ],
    ret: "Sets the checkmark of a menu item.",
    ex: 'echo "ui_menu_check($item, true);";',
    out: "ui_menu_check($item, true);",
    nocode: true,
    see: ["ui_menu_item", "ui_check_set"],
  },
  ui_on: {
    params: [{
        n: "handle",
        t: "int",
        d: "control/menu-item/window handle"
      },
      {
        n: "event",
        t: "string",
        d: "\"click\", \"change\", \"select\", \"close\", …"
      },
      {
        n: "callback",
        t: "closure",
        d: "called when the event fires"
      },
    ],
    ret: "Registers an event handler.",
    ex: 'echo "ui_on($btn, \"click\", fn() => ui_set($lbl, \"clicked!\"));";',
    out: "ui_on($btn, \"click\", fn() => ui_set($lbl, \"clicked!\"));",
    nocode: true,
    see: ["ui_timer", "ui_run_main"],
  },
  ui_timer: {
    params: [{
        n: "ms",
        t: "int",
        d: "interval in milliseconds"
      },
      {
        n: "callback",
        t: "closure",
        d: "called every interval"
      },
    ],
    ret: "Starts a repeating timer until ui_quit().",
    ex: 'echo "ui_timer(1000, fn() => ui_set($clock, date(\"H:i:s\")));";',
    out: "ui_timer(1000, fn() => ui_set($clock, date(\"H:i:s\")));",
    nocode: true,
    see: ["ui_on", "ui_run_main"],
  },
  ui_msg: {
    params: [{
        n: "win",
        t: "int",
        d: "parent window handle"
      },
      {
        n: "title",
        t: "string",
        d: "dialog title"
      },
      {
        n: "text",
        t: "string",
        d: "dialog message"
      },
      {
        n: "type",
        t: "int",
        req: false,
        d: "0 info 1 warning 2 error 3 question"
      },
    ],
    ret: "Shows a message box (blocks until dismissed).",
    ex: 'echo "ui_msg($win, \"Saved\", \"All changes stored.\");";',
    out: "ui_msg($win, \"Saved\", \"All changes stored.\");",
    nocode: true,
    see: ["ui_open_file", "ui_save_file"],
  },
  ui_open_file: {
    params: [{
      n: "title",
      t: "string",
      d: "dialog title"
    }],
    ret: "Chosen file path, or false when cancelled.",
    ex: 'echo "$p = ui_open_file(\"Open project\"); // false = cancelled";',
    out: "$p = ui_open_file(\"Open project\"); // false = cancelled",
    nocode: true,
    see: ["ui_save_file", "ui_pick_folder"],
  },
  ui_save_file: {
    params: [{
      n: "title",
      t: "string",
      d: "dialog title"
    }],
    ret: "Chosen save path, or false when cancelled.",
    ex: 'echo "$p = ui_save_file(\"Save as\");";',
    out: "$p = ui_save_file(\"Save as\");",
    nocode: true,
    see: ["ui_open_file"],
  },
  ui_pick_folder: {
    params: [{
      n: "title",
      t: "string",
      d: "dialog title"
    }],
    ret: "Chosen folder path, or false when cancelled.",
    ex: 'echo "$d = ui_pick_folder(\"Choose workspace\");";',
    out: "$d = ui_pick_folder(\"Choose workspace\");",
    nocode: true,
    see: ["ui_open_file"],
  },
  ui_pick_color: {
    params: [],
    ret: "Chosen color as RGB int, or false when cancelled.",
    ex: 'echo "$c = ui_pick_color(); // false = cancelled";',
    out: "$c = ui_pick_color(); // false = cancelled",
    nocode: true,
    see: ["ui_bg", "ui_fg"],
  },
  ui_clip_set: {
    params: [{
      n: "text",
      t: "string",
      d: "text for the clipboard"
    }],
    ret: "Copies to the system clipboard.",
    ex: 'echo "ui_clip_set(\"copied!\");";',
    out: "ui_clip_set(\"copied!\");",
    nocode: true,
    see: ["ui_clip_get"],
  },
  ui_clip_get: {
    params: [],
    ret: "Current clipboard text.",
    ex: 'echo "ui_clip_get(); // read clipboard";',
    out: "ui_clip_get(); // read clipboard",
    nocode: true,
    see: ["ui_clip_set"],
  },

  /* ============ Unsafe / FFI ============ */
  free: {
    params: [{
      n: "ptr",
      t: "pointer",
      d: "raw pointer from FFI"
    }],
    ret: "Frees the memory. Double-free = corruption; that is why this needs unsafe.",
    ex: 'echo "// unsafe { free($p); }";',
    out: "// unsafe { free($p); }",
    nocode: true,
    see: ["ffi_load", "memcpy"],
  },
  memcpy: {
    params: [{
        n: "dst",
        t: "pointer",
        d: "destination address"
      },
      {
        n: "src",
        t: "pointer",
        d: "source address"
      },
      {
        n: "n",
        t: "int",
        d: "byte count"
      },
    ],
    ret: "Copies $n raw bytes.",
    ex: 'echo "// unsafe { memcpy($dst, $src, $n); }";',
    out: "// unsafe { memcpy($dst, $src, $n); }",
    nocode: true,
    see: ["memset", "free"],
  },
  memset: {
    params: [{
        n: "dst",
        t: "pointer",
        d: "target address"
      },
      {
        n: "byte",
        t: "int",
        d: "fill byte"
      },
      {
        n: "n",
        t: "int",
        d: "byte count"
      },
    ],
    ret: "Fills $n bytes with $byte.",
    ex: 'echo "// unsafe { memset($buf, 0, 1024); }";',
    out: "// unsafe { memset($buf, 0, 1024); }",
    nocode: true,
    see: ["memcpy"],
  },
  ffi_load: {
    params: [{
      n: "path",
      t: "string",
      d: "shared library: \"mylib.dll\" / \"libmy.so\""
    }],
    ret: "Library handle for ffi_call(), or false on failure. Requires unsafe.",
    ex: 'echo "// unsafe { $lib = ffi_load(\"user32.dll\"); }";',
    out: "// unsafe { $lib = ffi_load(\"user32.dll\"); }",
    nocode: true,
    see: ["ffi_call", "free"],
  },
  ffi_call: {
    params: [{
        n: "lib",
        t: "resource",
        d: "handle from ffi_load()"
      },
      {
        n: "symbol",
        t: "string",
        d: "exported function name"
      },
    ],
    ret: "Callable for the native function. Requires unsafe.",
    ex: 'echo "// unsafe { $fn = ffi_call($lib, \"MessageBoxA\"); }";',
    out: "// unsafe { $fn = ffi_call($lib, \"MessageBoxA\"); }",
    nocode: true,
    see: ["ffi_load"],
  },
  heap_dump: {
    params: [],
    ret: "Debug dump of the runtime heap (allocation count, sizes).",
    ex: 'echo strlen(heap_dump()) > 0 ? "heap-info" : "empty";',
    out: "heap-info",
    see: ["memory_get_usage", "gc_collect_cycles"],
  },

  str_repeat: {
    params: [
      { n: "s", t: "string", d: "string to repeat" },
      { n: "n", t: "int", d: "how many times" },
    ],
    ret: "The string repeated $n times (empty when n <= 0).",
    ex: 'echo str_repeat("ab", 3), "\n";\necho "[" . str_repeat("-", 5) . "]", "\n";',
    out: "ababab\n[-----]",
    see: ["implode", "str_pad"],
  },
  explode: {
    params: [
      { n: "separator", t: "string", d: "delimiter (cannot be empty)" },
      { n: "s", t: "string", d: "string to split" },
    ],
    ret: "Array of the pieces between separators.",
    ex: '$p = explode(",", "a,b,c");\necho count($p), " ", $p[1], "\n";\necho explode("/", "2024/03/15")[2], "\n";',
    out: "3 b\n15",
    see: ["implode", "preg_split", "str_split"],
  },
  split: {
    params: [
      { n: "separator", t: "string", d: "delimiter" },
      { n: "s", t: "string", d: "string to split" },
    ],
    ret: "Alias of explode().",
    ex: 'echo implode("+", split("-", "1-2-3"));',
    out: "1+2+3",
    see: ["explode"],
  },
  str_split: {
    params: [
      { n: "s", t: "string", d: "string to cut" },
      { n: "len", t: "int", req: false, d: "chunk length (default 1)" },
    ],
    ret: "Array of chunks.",
    ex: 'echo implode("|", str_split("abc")), "\n";\necho implode("|", str_split("abcdef", 2)), "\n";',
    out: "a|b|c\nab|cd|ef",
    see: ["explode", "str_repeat"],
  },
  glob: {
    params: [{ n: "pattern", t: "string", d: "wildcard pattern like \"*.hphp\"" }],
    ret: "Matching paths as an array (empty when nothing matches).",
    ex: '$f = glob("tests/positive/*.hphp");\necho count($f) >= 5 ? "found-tests" : "none";',
    out: "found-tests",
    see: ["scandir", "is_file", "opendir"],
  },
  stream_ready: {
    params: [{ n: "handles", t: "array", d: "socket handles to check" }],
    ret: "Array of the handles that are ready to read (select-style).",
    ex: 'echo "used in lib/websocket.hphp for multiplexed sockets";',
    out: "used in lib/websocket.hphp for multiplexed sockets",
    nocode: true,
    see: ["stream_recv", "stream_socket_server"],
  },
  decbin: {
    params: [{ n: "n", t: "int", d: "non-negative integer" }],
    ret: "Binary string.",
    ex: 'echo decbin(10), " ", decbin(255);',
    out: "1010 11111111",
    see: ["bindec", "dechex", "decoct"],
  },
  decoct: {
    params: [{ n: "n", t: "int", d: "non-negative integer" }],
    ret: "Octal string.",
    ex: 'echo decoct(8), " ", decoct(511);',
    out: "10 777",
    see: ["octdec", "dechex", "decbin"],
  },
  dechex: {
    params: [{ n: "n", t: "int", d: "non-negative integer" }],
    ret: "Hex string (lowercase).",
    ex: 'echo dechex(255), " ", dechex(16);',
    out: "ff 10",
    see: ["hexdec", "decbin", "decoct"],
  },
  ui_dispatch: {
    params: [],
    ret: "Runs one iteration of the UI event loop and returns an event count.",
    ex: 'echo "manual loop: while (ui_alive()) { ui_dispatch(); }";',
    out: "manual loop: while (ui_alive()) { ui_dispatch(); }",
    nocode: true,
    see: ["ui_run_main", "ui_alive"],
  },
  malloc: {
    params: [{ n: "n", t: "int", d: "byte count" }],
    ret: "Raw pointer to n bytes (unsafe block required). Free with free().",
    ex: 'echo "// unsafe { $p = malloc(1024); memset($p, 0, 1024); free($p); }";',
    out: "// unsafe { $p = malloc(1024); memset($p, 0, 1024); free($p); }",
    nocode: true,
    see: ["free", "memset", "memcpy", "ffi_load"],
  },

  php_sapi: {
    params: [],
    ret: "Alias of php_sapi_name() - the interface name (\"cli\" for command-line builds).",
    ex: 'echo php_sapi() == "cli" ? "cli" : "other";',
    out: "cli",
    see: ["php_sapi_name", "phpversion"],
  },
    /* ============ Other / legacy ============ */
    php_logo_guid: {
      params: [],
      ret: "PHP-compat novelty; returns an identifier string. Safe to ignore.",
      ex: 'echo strlen(php_logo_guid()) >= 0 ? "exists" : "?";',
      out: "exists",
      see: ["phpversion"],
    },
  };