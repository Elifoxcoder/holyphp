#!/usr/bin/env python3
"""Cut benchmarks/challenge/fixture/feed.db out of the full challenge seed.

    python3 benchmarks/challenge/fixture/make-fixture.py <path-to-seed-dir>

<path-to-seed-dir> holds the upstream seed/feed.db and schema.sql from the
twelve-dollar challenge (seed/make-seed.sh in that tree builds them; the full
database is ~250 MB). The committed fixture keeps exactly the rows the 42-check
suite reads: the 20 posts of the golden /feed, posts 1, 250000 and 500000, the
authors of those posts, user 1 (the test's token), and every like on those
posts. Their values are copied verbatim from the deterministic seed, so
test/golden/*.json match byte for byte.

The fixture is committed, so running the suite needs neither the seed nor
Python. Re-run this only when the golden files or the challenge seed change.
"""
import json
import os
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "feed.db")


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[2])
        return 1
    seed_dir = os.path.abspath(sys.argv[1])
    seed_db = os.path.join(seed_dir, "feed.db")
    schema = os.path.join(seed_dir, "schema.sql")
    if not os.path.exists(schema):
        schema = os.path.join(os.path.dirname(seed_dir), "schema.sql")
    for p in (seed_db, schema):
        if not os.path.exists(p):
            print("missing", p)
            return 1
    golden = os.path.join(HERE, "..", "test", "golden", "feed.json")
    feed_ids = [p["id"] for p in json.load(open(golden))["posts"]]
    post_ids = feed_ids + [1, 250000, 500000]
    post_ids = sorted(set(post_ids))

    src = sqlite3.connect("file:" + seed_db + "?mode=ro", uri=True)
    q = ",".join("?" * len(post_ids))
    posts = src.execute(
        "SELECT id, user_id, body, created_at FROM posts WHERE id IN (%s)" % q,
        post_ids).fetchall()
    user_ids = sorted({r[1] for r in posts} | {1})
    qu = ",".join("?" * len(user_ids))
    users = src.execute(
        "SELECT id, username, created_at FROM users WHERE id IN (%s)" % qu,
        user_ids).fetchall()
    likes = src.execute(
        "SELECT user_id, post_id, created_at FROM likes WHERE post_id IN (%s)" % q,
        post_ids).fetchall()
    src.close()

    if len(feed_ids) != 20 or len(posts) != len(post_ids):
        print("unexpected seed contents: %d feed ids, %d posts, %d wanted"
              % (len(feed_ids), len(posts), len(post_ids)))
        return 1

    if os.path.exists(OUT):
        os.remove(OUT)
    for suffix in ("-wal", "-shm"):
        if os.path.exists(OUT + suffix):
            os.remove(OUT + suffix)
    con = sqlite3.connect(OUT)
    con.executescript(open(schema).read())
    con.executemany("INSERT INTO users VALUES (?,?,?)", users)
    con.executemany("INSERT INTO posts VALUES (?,?,?,?)", posts)
    con.executemany("INSERT INTO likes VALUES (?,?,?)", likes)
    con.commit()
    con.execute("VACUUM")
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    con.close()
    print("wrote %s: %d users, %d posts, %d likes, %d bytes"
          % (OUT, len(users), len(posts), len(likes), os.path.getsize(OUT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
