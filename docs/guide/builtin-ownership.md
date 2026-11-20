# Builtin ownership

Every builtin in `src/hphpc/builtins.c` is listed here with the ownership
contract of its result and its arguments. The audit is checked in two ways:
the positive `tests/positive/ownership_*.hphp` groups exercise the contracts
(including release of every returned value), and `tests/rss.ps1` /
`tests/rss.sh` prove that a 20000-round loop over each group stays flat.

## Reading the table

| Column | Meaning |
|---|---|
| **Returns** | `new string`, `new array`, `scalar`, `new handle`, `new value`: the caller owns the result and may release it (`str_free`, `arr_free`, `object_free`, `iarr_release`, ...). `borrowed element`: the value stays owned by the argument array; do not release it. `same string (borrowed)`: the call returns its argument. `raw pointer (unsafe)`: the value is only usable in `unsafe { }`. |
| **Arguments** | `borrows`: the call reads the value and leaves it untouched. `borrows and mutates`: the call rewrites the value in place (the caller keeps ownership). `takes`: the call releases or consumes the value/handle; using it afterwards is a use-after-free. |

All `*_ptr` and `*_fast` builtins, the raw memory accessors, and the manual
release/take builtins (`str_free`, `arr_free`, `object_free`, `iarr_release`,
`sb_discard`, `sb_take`, the B-tree release calls, `scope_end`) are marked
`unsafe` in the builtin table; `scripts/check-repo.sh` enforces the flag and
the wording on their documentation page. `tests/positive/ownership_arrays_move.hphp`
and the string copy tests show the other side: a released result leaves the
borrowed input alive.

Two limitations of this core (STATUS.md F1/F2, questions for the memory-core
owner) matter when reading the tests:

* assigning an array-typed value clones it (`codegen.c`) and the temporary is
  not released, so array-returning builtins are tested for values and
  element ownership rather than with the RSS loop;
* a string literal or a temporary expression inside a loop is a fresh heap
  object and is not reclaimed automatically, so the loop tests hoist their
  literals.

## Summary

| Group | Builtins |
|---|---|
| Strings | 78 |
| Arrays | 50 |
| Math and type juggling | 55 |
| JSON | 5 |
| File and I/O | 31 |
| Regex | 5 |
| Date and time | 8 |
| Network and transport | 46 |
| Misc: builders, threads, SQLite, UI and runtime | 269 |
| **Total** | **547** |

## Strings

| Builtin | Returns | Arguments |
|---|---|---|
| `strlen` | scalar | borrows |
| `strtoupper` | new string | borrows |
| `strtolower` | new string | borrows |
| `ucfirst` | new string | borrows |
| `lcfirst` | new string | borrows |
| `trim` | new string | borrows |
| `ltrim` | new string | borrows |
| `rtrim` | new string | borrows |
| `chop` | new string | borrows |
| `strrev` | new string | borrows |
| `str_repeat` | new string | borrows |
| `str_pad` | new string | borrows |
| `str_replace` | new string | borrows |
| `substr` | new string | borrows |
| `strstr` | new string | borrows |
| `strchr` | new string | borrows |
| `strpos` | scalar | borrows |
| `str_contains` | scalar | borrows |
| `str_starts_with` | scalar | borrows |
| `str_ends_with` | scalar | borrows |
| `strcmp` | scalar | borrows |
| `strncmp` | scalar | borrows |
| `strncasecmp` | scalar | borrows |
| `strcasecmp` | scalar | borrows |
| `explode` | new array | borrows |
| `split` | new array | borrows |
| `implode` | new string | borrows |
| `join` | new string | borrows |
| `nl2br` | new string | borrows |
| `number_format` | new string | borrows |
| `sha256` | new string | borrows |
| `hash_hmac` | new string or false | borrows |
| `password_hash` | new string | borrows |
| `password_verify` | scalar | borrows |
| `random_bytes` | new string | borrows |
| `hash_equals` | scalar | borrows |
| `md5` | new string | borrows |
| `sha1` | new string | borrows |
| `crc32` | scalar | borrows |
| `base64_encode` | new string | borrows |
| `base64_decode` | new string | borrows |
| `urlencode` | new string | borrows |
| `urldecode` | new string | borrows |
| `htmlentities` | new string | borrows |
| `htmlspecialchars` | new string | borrows |
| `sprintf` | new string | borrows |
| `printf` | scalar | borrows |
| `vsprintf` | new string | borrows |
| `ord` | scalar | borrows |
| `chr` | new string | borrows |
| `bin2hex` | new string | borrows |
| `hex2bin` | new string | borrows |
| `str_split` | new array | borrows |
| `ucwords` | new string | borrows |
| `wordwrap` | new string | borrows |
| `similar_text` | scalar | borrows |
| `levenshtein` | scalar | borrows |
| `substr_count` | scalar | borrows |
| `str_shuffle` | new string | borrows |
| `strip_tags` | new string | borrows |
| `html_entity_decode` | new string | borrows |
| `decbin` | new string | borrows |
| `decoct` | new string | borrows |
| `dechex` | new string | borrows |
| `bindec` | scalar | borrows |
| `octdec` | scalar | borrows |
| `hexdec` | scalar | borrows |
| `str_at_eq` | scalar | borrows |
| `str_find_range` | scalar | borrows |
| `str_free (*unsafe*)` | scalar | **takes** the value/handle |
| `str_from_bytes (*unsafe*)` | new string | borrows |
| `str_int_range` | scalar | borrows |
| `str_persist` | same string (borrowed) | borrows |
| `str_pos_ci` | scalar | borrows |
| `str_prefix_ci` | scalar | borrows |
| `str_to_int` | scalar | borrows |
| `str_to_int_range` | scalar | borrows |
| `str_trim_range` | new string | borrows |

## Arrays

| Builtin | Returns | Arguments |
|---|---|---|
| `count` | scalar | borrows |
| `sizeof` | scalar | borrows |
| `array` | new array | borrows |
| `array_keys` | new array | borrows |
| `array_values` | new array | borrows |
| `array_merge` | new array | borrows |
| `array_slice` | new array | borrows |
| `array_reverse` | new array | borrows |
| `array_sum` | new value | borrows |
| `array_product` | new value | borrows |
| `array_unique` | new array | borrows |
| `in_array` | scalar | borrows |
| `array_key_exists` | scalar | borrows |
| `isset` | scalar | borrows |
| `range` | new array | borrows |
| `compact` | new array | borrows |
| `array_column` | new array | borrows |
| `array_chunk` | new array | borrows |
| `array_pad` | new array | borrows |
| `array_replace` | new array | borrows |
| `array_fill_keys` | new array | borrows |
| `array_key_first` | borrowed element | borrows |
| `array_key_last` | borrowed element | borrows |
| `usort` | scalar | borrows and mutates |
| `uasort` | scalar | borrows and mutates |
| `uksort` | scalar | borrows and mutates |
| `array_walk` | scalar | borrows and mutates |
| `array_walk_recursive` | scalar | borrows and mutates |
| `array_map` | new array | borrows array and callback |
| `array_filter` | new array | borrows array and callback |
| `array_reduce` | callback accumulator | borrows array and callback |
| `array_flip` | new array | borrows |
| `array_fill` | new array | borrows |
| `array_combine` | new array | borrows |
| `array_diff` | new array | borrows |
| `array_intersect` | new array | borrows |
| `array_push` | scalar | borrows and mutates |
| `array_pop` | moved out (caller owns) | borrows and mutates |
| `array_shift` | moved out (caller owns) | borrows and mutates |
| `array_unshift` | scalar | borrows and mutates |
| `array_splice` | new array | borrows and mutates |
| `shuffle` | scalar | borrows and mutates |
| `sort` | scalar | borrows and mutates |
| `rsort` | scalar | borrows and mutates |
| `ksort` | scalar | borrows and mutates |
| `krsort` | scalar | borrows and mutates |
| `asort` | scalar | borrows and mutates |
| `array_search` | borrowed element | borrows |
| `end` | borrowed element | borrows |
| `reset` | borrowed element | borrows |

## Math and type juggling

| Builtin | Returns | Arguments |
|---|---|---|
| `max` | borrowed element | borrows |
| `min` | borrowed element | borrows |
| `rand` | scalar | borrows |
| `mt_rand` | scalar | borrows |
| `srand` | nothing | borrows |
| `mt_srand` | nothing | borrows |
| `random_int` | scalar | borrows |
| `fgetc` | new string | borrows |
| `abs` | new value | borrows |
| `round` | new value | borrows |
| `floor` | scalar | borrows |
| `ceil` | scalar | borrows |
| `sqrt` | scalar | borrows |
| `pow` | new value | borrows |
| `intdiv` | scalar | borrows |
| `fmod` | scalar | borrows |
| `sin` | scalar | borrows |
| `cos` | scalar | borrows |
| `tan` | scalar | borrows |
| `atan` | scalar | borrows |
| `atan2` | scalar | borrows |
| `asin` | scalar | borrows |
| `acos` | scalar | borrows |
| `log` | scalar | borrows |
| `log2` | scalar | borrows |
| `log10` | scalar | borrows |
| `exp` | scalar | borrows |
| `is_nan` | scalar | borrows |
| `is_finite` | scalar | borrows |
| `is_infinite` | scalar | borrows |
| `pi` | scalar | borrows |
| `M_PI` | scalar | borrows |
| `deg2rad` | scalar | borrows |
| `rad2deg` | scalar | borrows |
| `hypot` | scalar | borrows |
| `lcg_value` | scalar | borrows |
| `is_int` | scalar | borrows |
| `is_integer` | scalar | borrows |
| `is_float` | scalar | borrows |
| `is_string` | scalar | borrows |
| `is_bool` | scalar | borrows |
| `is_array` | scalar | borrows |
| `is_null` | scalar | borrows |
| `is_numeric` | scalar | borrows |
| `intval` | scalar | borrows |
| `floatval` | scalar | borrows |
| `doubleval` | scalar | borrows |
| `strval` | new string | borrows |
| `boolval` | scalar | borrows |
| `gettype` | new string | borrows |
| `var_dump` | nothing | borrows |
| `print_r` | new string | borrows |
| `var_export` | new string | borrows |
| `serialize` | new string | borrows |
| `unserialize` | new value | borrows |

## JSON

| Builtin | Returns | Arguments |
|---|---|---|
| `json_encode` | new string | borrows |
| `json_encode_pretty` | new string | borrows |
| `json_decode` | new value tree | borrows |
| `json_num_field` | scalar | borrows |
| `json_str_field` | new string | borrows |

## File and I/O

| Builtin | Returns | Arguments |
|---|---|---|
| `readline` | new string | borrows |
| `file_get_contents` | new string or false | borrows |
| `file_put_contents` | new value | borrows |
| `file_exists` | scalar | borrows |
| `is_dir` | scalar | borrows |
| `filesize` | scalar | borrows |
| `mkdir` | scalar | borrows |
| `rmdir` | scalar | borrows |
| `unlink` | scalar | borrows |
| `rename` | scalar | borrows |
| `copy` | new value | borrows |
| `fgets` | new string or false | borrows |
| `fwrite` | scalar | borrows |
| `fopen` | new handle | borrows |
| `fclose` | scalar | **takes** the value/handle |
| `scandir` | new array | borrows |
| `dirname` | new string | borrows |
| `basename` | new string | borrows |
| `pathinfo` | new value | borrows |
| `realpath` | new value | borrows |
| `is_file` | scalar | borrows |
| `is_readable` | scalar | borrows |
| `is_writable` | scalar | borrows |
| `touch` | scalar | borrows |
| `glob` | new array | borrows |
| `sys_get_temp_dir` | new string | borrows |
| `checkdate` | scalar | borrows |
| `fread` | new string | borrows |
| `feof` | scalar | borrows |
| `getcwd` | new string | borrows |
| `putenv` | scalar | borrows and mutates |

## Regex

| Builtin | Returns | Arguments |
|---|---|---|
| `preg_match` | new value | borrows |
| `preg_match_all` | new value | borrows |
| `preg_replace` | new value | borrows |
| `preg_split` | new value | borrows |
| `preg_grep` | new array (borrows elements) | borrows array and pattern |

## Date and time

| Builtin | Returns | Arguments |
|---|---|---|
| `mktime` | scalar | borrows |
| `strtotime` | scalar | borrows |
| `microtime` | scalar | borrows |
| `hrtime` | scalar | borrows |
| `date` | new string | borrows |
| `time` | scalar | borrows |
| `usleep` | nothing | borrows |
| `sleep` | scalar | borrows |

## Network and transport

| Builtin | Returns | Arguments |
|---|---|---|
| `http_post` | new value | borrows |
| `parse_url` | new value | borrows |
| `http_build_query` | new string | borrows |
| `mysql_connect` | new handle | borrows |
| `mysql_query` | new value | borrows |
| `mysql_exec` | new value | borrows |
| `mysql_insert_id` | scalar | borrows |
| `mysql_close` | scalar | borrows |
| `mysql_escape` | new string | borrows |
| `http_get` | new string or false | borrows |
| `http_request` | new value | borrows |
| `stream_socket_server` | new handle | borrows |
| `stream_socket_client` | new handle | borrows |
| `stream_socket_accept` | new handle | borrows |
| `stream_socket_accept2` | new handle | borrows |
| `stream_set_blocking` | scalar | borrows and mutates |
| `stream_ready` | new array | borrows |
| `stream_recv` | new string | borrows |
| `stream_send` | scalar | borrows |
| `stream_eof` | scalar | borrows |
| `stream_peer` | new string | borrows |
| `stream_close` | scalar | borrows |
| `accept_emfile` | scalar | borrows |
| `epoll_add` | scalar | borrows |
| `epoll_del` | scalar | borrows |
| `epoll_mod` | scalar | borrows |
| `epoll_wait2` | scalar | borrows |
| `epoll_wait_iarr2 (*unsafe*)` | scalar | borrows |
| `epoll_wait_into` | scalar | borrows |
| `http_request_bounds` | scalar | borrows |
| `jwt_verifier_new` | new string | borrows |
| `jwt_verify` | new string | borrows |
| `jwt_verify_prepared` | new string | borrows |
| `nofile_limit` | scalar | borrows |
| `stream_ready_into (*unsafe*)` | scalar | borrows |
| `stream_recv_release` | scalar | **takes** the value/handle |
| `stream_send2` | scalar | borrows |
| `stream_send2_nb` | scalar | borrows |
| `stream_send_nb` | scalar | borrows |
| `stream_send_sb` | scalar | **takes** the value/handle |
| `stream_set_nodelay` | scalar | borrows and mutates |
| `stream_set_socket_buffers` | scalar | borrows and mutates |
| `stream_socket_server_reuseport` | new handle | borrows |
| `uring_active` | scalar | borrows |
| `uring_direct` | scalar | borrows |
| `uring_direct_slots` | scalar | borrows |

## Misc: builders, threads, SQLite, UI and runtime

| Builtin | Returns | Arguments |
|---|---|---|
| `unset` | nothing | borrows and mutates |
| `getenv` | new string | borrows |
| `php_uname` | new string | borrows |
| `phpversion` | new string | borrows |
| `php_sapi_name` | new string | borrows |
| `memory_get_usage` | scalar | borrows |
| `memory_get_peak_usage` | scalar | borrows |
| `gc_collect_cycles` | scalar | borrows |
| `sys_getloadavg` | new value | borrows |
| `php_logo_guid` | new string | borrows |
| `exit` | nothing | borrows |
| `die` | nothing | borrows |
| `php_sapi` | new string | borrows |
| `ui_dispatch` | scalar or handle | borrows |
| `ui_run_main` | nothing | borrows |
| `ui_quit` | nothing | borrows |
| `ui_window` | new handle | borrows |
| `ui_show` | scalar or handle | borrows |
| `ui_title` | scalar or handle | borrows |
| `ui_size` | scalar or handle | borrows |
| `ui_pos` | scalar or handle | borrows |
| `ui_close` | scalar or handle | borrows |
| `ui_alive` | scalar or handle | borrows |
| `ui_add` | new handle | borrows |
| `ui_set` | scalar or handle | borrows |
| `ui_get` | new string | borrows |
| `ui_enable` | scalar or handle | borrows |
| `ui_move` | scalar or handle | borrows |
| `ui_focus` | scalar or handle | borrows |
| `ui_select_all` | scalar or handle | borrows |
| `ui_items` | scalar or handle | borrows |
| `ui_remove` | scalar or handle | borrows |
| `ui_clear` | scalar or handle | borrows |
| `ui_ctrl_show` | scalar or handle | borrows |
| `ui_selected` | scalar or handle | borrows |
| `ui_select` | scalar or handle | borrows |
| `ui_item_text` | new string | borrows |
| `ui_check_get` | scalar or handle | borrows |
| `ui_check_set` | scalar or handle | borrows |
| `ui_progress` | scalar or handle | borrows |
| `ui_slider_get` | scalar or handle | borrows |
| `ui_slider_set` | scalar or handle | borrows |
| `ui_bg` | scalar or handle | borrows |
| `ui_fg` | scalar or handle | borrows |
| `ui_font` | scalar or handle | borrows |
| `ui_menu` | scalar or handle | borrows |
| `ui_menu_item` | scalar or handle | borrows |
| `ui_menu_sep` | scalar or handle | borrows |
| `ui_menu_check` | scalar or handle | borrows |
| `ui_on` | scalar or handle | borrows |
| `ui_timer` | scalar or handle | borrows |
| `ui_msg` | scalar or handle | borrows |
| `ui_open_file` | scalar or handle | borrows |
| `ui_save_file` | scalar or handle | borrows |
| `ui_pick_folder` | scalar or handle | borrows |
| `ui_pick_color` | scalar or handle | borrows |
| `ui_clip_set` | scalar or handle | borrows |
| `ui_clip_get` | new handle | borrows |
| `ui_theme` | scalar or handle | borrows |
| `ui_style` | scalar or handle | borrows |
| `ui_accent` | scalar or handle | borrows |
| `ui_radius` | scalar or handle | borrows |
| `ui_range` | scalar or handle | borrows |
| `ui_repaint` | scalar or handle | borrows |
| `ui_input_margin` | scalar or handle | borrows |
| `ui_raise` | scalar or handle | borrows |
| `ui_svg` | scalar or handle | borrows |
| `ui_picture` | scalar or handle | borrows |
| `ui_text_px` | scalar or handle | borrows |
| `ui_text_lines` | scalar or handle | borrows |
| `ui_enabled` | scalar or handle | borrows |
| `ui_rect` | new array | borrows |
| `ui_client_size` | new array | borrows |
| `ui_scroll` | new array | borrows |
| `ui_count` | scalar or handle | borrows |
| `ui_progress_get` | scalar or handle | borrows |
| `ui_hwnd` | new handle | borrows |
| `ui_font_new` | new handle | borrows |
| `ui_font_width` | scalar or handle | borrows |
| `ui_font_height` | scalar or handle | borrows |
| `ui_draw_clear` | scalar or handle | borrows |
| `ui_draw_rect` | scalar or handle | borrows |
| `ui_draw_round` | scalar or handle | borrows |
| `ui_draw_line` | scalar or handle | borrows |
| `ui_draw_text` | scalar or handle | borrows |
| `ui_draw_clip` | scalar or handle | borrows |
| `ui_draw_unclip` | scalar or handle | borrows |
| `ui_canvas_size` | new array | borrows |
| `ui_canvas_pixel` | scalar or handle | borrows |
| `ui_canvas_invalidate` | scalar or handle | borrows |
| `ui_canvas_flush` | scalar or handle | borrows |
| `ui_cursor` | scalar or handle | borrows |
| `shell_exec` | new value | borrows |
| `exec` | new value | borrows |
| `passthru` | nothing | borrows |
| `RESET` | scalar | borrows |
| `BLACK` | scalar | borrows |
| `RED` | scalar | borrows |
| `GREEN` | scalar | borrows |
| `YELLOW` | scalar | borrows |
| `BLUE` | scalar | borrows |
| `MAGENTA` | scalar | borrows |
| `CYAN` | scalar | borrows |
| `WHITE` | scalar | borrows |
| `BRIGHT_BLACK` | scalar | borrows |
| `BRIGHT_RED` | scalar | borrows |
| `BRIGHT_GREEN` | scalar | borrows |
| `BRIGHT_YELLOW` | scalar | borrows |
| `BRIGHT_BLUE` | scalar | borrows |
| `BRIGHT_MAGENTA` | scalar | borrows |
| `BRIGHT_CYAN` | scalar | borrows |
| `BRIGHT_WHITE` | scalar | borrows |
| `print` | nothing | borrows |
| `println` | nothing | borrows |
| `input` | new string | borrows |
| `proc_open` | new handle | borrows |
| `proc_get_status` | new handle | borrows |
| `proc_start` | new handle | borrows |
| `proc_read` | new string | borrows |
| `proc_read_err` | new string | borrows |
| `proc_write` | scalar or handle | borrows |
| `proc_alive` | scalar or handle | borrows |
| `proc_exit` | scalar or handle | borrows |
| `proc_close` | scalar or handle | **takes** the value/handle |
| `proc_kill` | scalar or handle | **takes** the value/handle |
| `ui_browser_ready` | scalar or handle | borrows |
| `ui_browser_loader` | new string | borrows |
| `ui_browser_new` | new handle | borrows |
| `ui_browser_free` | scalar or handle | **takes** the value/handle |
| `ui_browser_fit` | scalar or handle | borrows |
| `ui_browser_go` | scalar or handle | borrows |
| `ui_browser_html` | scalar or handle | borrows |
| `ui_browser_back` | scalar or handle | borrows |
| `ui_browser_forward` | scalar or handle | borrows |
| `ui_browser_reload` | scalar or handle | borrows |
| `ui_browser_stop` | scalar or handle | borrows |
| `ui_browser_can_back` | scalar or handle | borrows |
| `ui_browser_can_forward` | scalar or handle | borrows |
| `ui_browser_url` | new string | borrows |
| `ui_browser_favicon` | new string | borrows |
| `ui_browser_title` | new string | borrows |
| `ui_browser_zoom` | scalar or handle | borrows |
| `ui_browser_zoom_get` | scalar or handle | borrows |
| `ui_browser_devtools` | scalar or handle | borrows |
| `ui_browser_devtools_fit` | scalar or handle | borrows |
| `ui_browser_devtools_ready` | scalar or handle | borrows |
| `ui_browser_print` | scalar or handle | borrows |
| `ui_browser_post` | scalar or handle | borrows |
| `ui_browser_on` | scalar or handle | borrows |
| `ui_browser_key` | scalar or handle | borrows |
| `ui_browser_visible` | scalar or handle | borrows |
| `thr_spawn` | scalar or handle | borrows |
| `thr_join` | scalar or handle | borrows |
| `thr_current` | scalar or handle | borrows |
| `thr_id` | scalar or handle | borrows |
| `thr_sleep_ms` | nothing | borrows |
| `thr_cpu_count` | scalar or handle | borrows |
| `thr_parallel_map` | scalar or handle | borrows |
| `chan_new` | new handle | borrows |
| `chan_send` | scalar or handle | borrows |
| `chan_recv` | scalar or handle | borrows |
| `chan_try_send` | scalar or handle | borrows |
| `chan_try_recv` | scalar or handle | borrows |
| `chan_close` | scalar or handle | **takes** the value/handle |
| `mutex_new` | new handle | borrows |
| `mutex_lock` | scalar or handle | borrows |
| `mutex_trylock` | scalar or handle | borrows |
| `mutex_unlock` | scalar or handle | borrows |
| `mutex_free` | scalar or handle | **takes** the value/handle |
| `barrier_new` | new handle | borrows |
| `barrier_wait` | scalar or handle | borrows |
| `atomic_new` | new handle | borrows |
| `atomic_get` | scalar or handle | borrows |
| `atomic_set` | scalar or handle | borrows and mutates |
| `atomic_add` | scalar or handle | borrows and mutates |
| `atomic_cas` | scalar or handle | borrows and mutates |
| `malloc (*unsafe*)` | raw pointer (unsafe) | borrows |
| `free (*unsafe*)` | nothing | **takes** the value/handle |
| `memcpy (*unsafe*)` | same pointer (borrowed) | borrows |
| `memset (*unsafe*)` | same pointer (borrowed) | borrows |
| `ffi_load (*unsafe*)` | raw pointer (unsafe) | borrows |
| `ffi_call (*unsafe*)` | raw pointer (unsafe) | borrows |
| `heap_dump (*unsafe*)` | new string | borrows |
| `arr_clear` | scalar | borrows and mutates |
| `arr_free (*unsafe*)` | scalar | **takes** the value/handle |
| `bt_close` | scalar or handle | **takes** the value/handle |
| `bt_col_int` | scalar or handle | borrows |
| `bt_col_int_at` | scalar or handle | borrows |
| `bt_col_len (*unsafe*)` | scalar or handle | borrows |
| `bt_col_off (*unsafe*)` | scalar or handle | borrows |
| `bt_cols` | scalar or handle | borrows |
| `bt_count_key` | scalar or handle | borrows |
| `bt_count_range` | scalar or handle | borrows |
| `bt_first` | scalar or handle | borrows |
| `bt_last` | scalar or handle | borrows |
| `bt_multi_keys` | scalar or handle | borrows |
| `bt_multi_payloads (*unsafe*)` | scalar or handle | borrows |
| `bt_multi_release (*unsafe*)` | scalar or handle | **takes** the value/handle |
| `bt_multi_seek (*unsafe*)` | new handle | borrows |
| `bt_next` | scalar or handle | borrows |
| `bt_open (*unsafe*)` | new handle | borrows |
| `bt_page_release (*unsafe*)` | scalar or handle | **takes** the value/handle |
| `bt_payload_pin (*unsafe*)` | new handle | borrows |
| `bt_payload_ptr (*unsafe*)` | raw pointer (unsafe) | borrows |
| `bt_payload_size` | scalar or handle | borrows |
| `bt_prev` | scalar or handle | borrows |
| `bt_read (*unsafe*)` | scalar or handle | borrows |
| `bt_root (*unsafe*)` | scalar or handle | borrows |
| `bt_rowid` | scalar or handle | borrows |
| `bt_seek` | scalar or handle | borrows |
| `bt_tail (*unsafe*)` | new handle | borrows |
| `iarr_data_ptr (*unsafe*)` | raw pointer (unsafe) | borrows |
| `iarr_get` | scalar or handle | borrows |
| `iarr_get_fast (*unsafe*)` | scalar or handle | borrows |
| `iarr_len` | scalar or handle | borrows |
| `iarr_new` | new handle | borrows |
| `iarr_new_uninit (*unsafe*)` | new handle | borrows |
| `iarr_release (*unsafe*)` | scalar or handle | **takes** the value/handle |
| `iarr_set` | scalar or handle | borrows and mutates |
| `iarr_set_fast (*unsafe*)` | scalar or handle | borrows and mutates |
| `int_digits` | scalar | borrows |
| `live_allocs` | new value | borrows |
| `live_bytes` | new value | borrows |
| `object_free (*unsafe*)` | scalar | **takes** the value/handle |
| `runtime_diag_write` | scalar | borrows |
| `runtime_stats` | new string | borrows |
| `sb_bytes (*unsafe*)` | scalar or handle | borrows |
| `sb_data_ptr (*unsafe*)` | raw pointer (unsafe) | borrows |
| `sb_discard (*unsafe*)` | scalar or handle | **takes** the value/handle |
| `sb_int` | scalar or handle | borrows and mutates |
| `sb_json` | scalar or handle | borrows and mutates |
| `sb_json_str` | scalar or handle | borrows and mutates |
| `sb_len` | scalar or handle | borrows |
| `sb_new` | new handle | borrows |
| `sb_prepend` | scalar or handle | borrows and mutates |
| `sb_put` | scalar or handle | borrows and mutates |
| `sb_reserve` | scalar or handle | borrows and mutates |
| `scalar_binop` | new value | borrows |
| `scope_begin` | scalar | borrows |
| `scope_end (*unsafe*)` | scalar | **takes** the value/handle |
| `sqlite_bg_checkpoint_active` | scalar or handle | borrows |
| `sqlite_bg_checkpoint_hold` | scalar or handle | borrows |
| `sqlite_bg_checkpoint_start` | scalar or handle | borrows |
| `sqlite_bg_checkpoint_tick` | scalar or handle | borrows |
| `sqlite_bind_int` | scalar or handle | borrows |
| `sqlite_bind_text` | scalar or handle | borrows |
| `sqlite_busy_timeout` | scalar or handle | borrows |
| `sqlite_changes` | scalar or handle | borrows |
| `sqlite_clear_bindings` | scalar or handle | borrows and mutates |
| `sqlite_close` | scalar or handle | **takes** the value/handle |
| `sqlite_column_int` | scalar or handle | borrows |
| `sqlite_column_text` | new string | borrows |
| `sqlite_errmsg` | scalar or handle | borrows |
| `sqlite_exec` | scalar or handle | borrows |
| `sqlite_extended_errcode` | scalar or handle | borrows |
| `sqlite_finalize` | scalar or handle | **takes** the value/handle |
| `sqlite_is_lock_error` | scalar or handle | borrows |
| `sqlite_last_insert_rowid` | scalar or handle | borrows |
| `sqlite_open (*unsafe*)` | new handle | borrows |
| `sqlite_open_vfs (*unsafe*)` | new handle | borrows |
| `sqlite_prepare (*unsafe*)` | new handle | borrows |
| `sqlite_reset` | scalar or handle | borrows and mutates |
| `sqlite_status` | new string | borrows |
| `sqlite_step` | scalar or handle | borrows |
| `sb_take (*unsafe*)` | new string | **takes** the value/handle |
| `u8_at (*unsafe*)` | raw pointer (unsafe) | borrows |
| `u8_set (*unsafe*)` | raw pointer (unsafe) | borrows |
| `i64_at (*unsafe*)` | raw pointer (unsafe) | borrows |
| `i64_set (*unsafe*)` | raw pointer (unsafe) | borrows |

