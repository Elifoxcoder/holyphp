#!/usr/bin/env python3
"""Build tests/positive/data/bt_fixture.db, the tiny database bt_cursor.hphp
reads through the B-tree builtins.

    python3 tests/positive/data/make_bt_fixture.py

The file is committed, so the test suite needs neither Python nor sqlite3 at
run time. Re-run this after changing the data and update the expectations in
tests/positive/bt_cursor.hphp."""
import os
import sqlite3

HERE = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(HERE, "bt_fixture.db")

SCHEMA = """
CREATE TABLE users (
  id       INTEGER PRIMARY KEY,
  username TEXT NOT NULL UNIQUE
);
CREATE TABLE posts (
  id         INTEGER PRIMARY KEY,
  user_id    INTEGER NOT NULL REFERENCES users(id),
  body       TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE likes (
  user_id  INTEGER NOT NULL REFERENCES users(id),
  post_id  INTEGER NOT NULL REFERENCES posts(id),
  PRIMARY KEY (user_id, post_id)
);
CREATE INDEX posts_created_at_id_idx ON posts (created_at DESC, id DESC);
CREATE INDEX likes_post_id_idx       ON likes (post_id);
CREATE INDEX users_name_idx          ON users (username);
"""

USERS = [
    (1, "alice"),
    (2, "bob"),
    (3, "carol"),
    (4, "dave"),
    (5, "erin"),
]
POSTS = [
    (1, 1, "first post", "2025-12-31T23:59:10.000Z"),
    (2, 2, "second post, a little longer", "2025-12-31T23:59:11.000Z"),
    (3, 1, "third post", "2025-12-31T23:59:12.000Z"),
    (4, 3, "fourth post", "2025-12-31T23:59:13.000Z"),
    (5, 2, "fifth post", "2025-12-31T23:59:14.000Z"),
    (6, 5, "sixth post", "2025-12-31T23:59:15.000Z"),
]
# post -> users who liked it: 1 -> 3, 2 -> 2, 3 -> 1, 4 -> 4, 5 -> 0, 6 -> 1
LIKES = [
    (1, 1), (2, 1), (3, 1),
    (2, 2), (4, 2),
    (5, 3),
    (1, 4), (2, 4), (3, 4), (4, 4),
    (1, 6),
]

if os.path.exists(DB):
    os.remove(DB)
con = sqlite3.connect(DB)
con.executescript(SCHEMA)
con.executemany("INSERT INTO users VALUES (?,?)", USERS)
con.executemany("INSERT INTO posts VALUES (?,?,?,?)", POSTS)
con.executemany("INSERT INTO likes VALUES (?,?)", LIKES)
con.commit()
con.execute("VACUUM")
con.close()
print("wrote", DB, os.path.getsize(DB), "bytes")
