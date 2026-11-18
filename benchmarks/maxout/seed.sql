-- seed.sql — 100k posts, same schema both servers create
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
DROP TABLE IF EXISTS posts;
CREATE TABLE posts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    author TEXT NOT NULL,
    content TEXT NOT NULL,
    likes INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
);
CREATE INDEX idx_posts_id ON posts(id DESC);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 100000)
INSERT INTO posts (author, content, likes, created_at)
SELECT 'user' || (x % 1000), 'benchmark post content number ' || x, x % 50, 1700000000 + x FROM c;
