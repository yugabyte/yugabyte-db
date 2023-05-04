Cutting a new release
---------------------

**NOTE:** This assumes you've run the tests and they pass.

1. Increase the version number.
    * [ ] Introduce `hll--<old>--<new>.sql`.
    * [ ] Change `CHANGELOG.md`.
    * [ ] Change `hll.control`.
    * [ ] Change `Makefile`.
    * [ ] Change `README.md`
    * [ ] Change `setup.sql` and `setup.out`
2. Commit changes and open a PR.
3. Tag the commit on master after the PR is merged. 'vX.Y.Z' is the name format.
