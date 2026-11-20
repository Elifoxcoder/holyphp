# Challenge fixture database

`feed.db` is a cut-down copy of the challenge's deterministic seed database
(`seed/feed.db`, about 250 MB, built by `seed/make-seed.sh` in the original
bundle). It contains exactly the rows `../test/test.sh` reads:

- the 20 posts in `test/golden/feed.json` and their authors;
- posts 1, 250000 and 500000 (`test/golden/post-*.json`);
- every like on those posts, which the golden `like_count` values depend on;
- user 1 (`golden_ember_1`), the user the suite's pre-signed token names.

The schema, indexes and column values are copied verbatim from the seed, so
the golden responses match byte for byte. The file is 36 KiB and committed;
running the benchmark needs neither the seed nor a generator.

## Regenerating

With the full challenge seed available (its `seed/feed.db` and `schema.sql`):

```bash
python3 benchmarks/challenge/fixture/make-fixture.py path/to/challenge/seed
```

The script refuses to run if the seed does not contain the expected posts.
Re-run `bash benchmarks/challenge/run.sh` afterwards; both modes must still
report `passed=42 failed=0`.
