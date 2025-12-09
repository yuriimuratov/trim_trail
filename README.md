# trim_tail

Command-line utility that rewrites a log file in place, keeping only the last **N** lines. The inode stays the same, so any process with the file open can keep writing to it after trimming.

## Build and install

```bash
cd /usr/src/trim_trail
make            # builds trim_tail from trim_tail.c
sudo make install  # installs to /usr/local/bin/trim_tail
```

`make clean` removes the binary and generated `version.h`. `make uninstall` removes the installed binary.

### Ubuntu/Debian prerequisites

```bash
sudo apt-get update
sudo apt-get install -y build-essential
```

## Versioning

`VERSION` holds the release string (currently `0.1.0`). Running `make` regenerates `version.h` from that file so `trim_tail --version` (or `-v`/`-V`) prints the same number.

## Usage

```bash
trim_tail LOGFILE N_LINES
trim_tail --version
```

- `N_LINES` must be an integer; values below zero are treated as zero.
- An exclusive `flock` is held for the duration of the rewrite to avoid concurrent truncation.
- For `N_LINES == 0`, the file is truncated to zero length and `fsync`'d.
- Otherwise the tool scans from the end in 64 KiB blocks to find the starting offset of the last `N_LINES`, copies that tail forward in 128 KiB chunks using the same file descriptor, then `ftruncate` + `fsync`.
- Files without a trailing newline keep their final partial line counted as one.
- Memory use is bounded by the chunk sizes; the approach works on large files (multi‑GB) because only the tail is buffered.

Errors are printed to stderr and a non-zero status is returned.

## Testing

```bash
make test
```

`tests/run_tests.sh` exercises key cases: trimming to zero, keeping a subset of lines, requesting more lines than exist, handling files without a trailing newline, empty files, negative `N_LINES` (treated as zero), and resilience to `EINTR`/short read/write via an `LD_PRELOAD` fault-injection shim built on the fly.

## Bumping the version

Update `VERSION`, then run `make` so the generated `version.h` embeds the new number.

## Release flow

- Ensure clean git tree.
- Run `NEW_VERSION=x.y.z ./scripts/release.sh` (or pass version as first arg). This:
  - writes `VERSION`
  - rebuilds with `VERSION_OVERRIDE` to avoid hash suffix
  - runs tests
  - commits `Release vX.Y.Z`
  - tags `vX.Y.Z`
- Push manually if desired:  
  `git push && git push --tags`

## License

Apache-2.0 © 2025 Yurii Muratov. See `LICENSE`.
