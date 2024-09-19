# To Build Your Own Debian Packages With Docker
Run `./packaging/build_packages.sh -h` and follow the instructions.
E.g. to build for Debian 12 and PostgreSQL 16, run:
```
./packaging/build_packages.sh --os deb12 --pg 16
```

Packages can be found at the `packages` directory by default, but that can be configured with the `--output-dir` option.

**Note:** The packages do not include pg_helio_distributed in the `internal` directory.
