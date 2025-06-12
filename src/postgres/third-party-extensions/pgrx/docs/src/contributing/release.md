```shell
PGRX_HOME=/tmp/pgrx-release ./prepare-release.sh NEW_VERSION_NUMBER
```

- go make a PR to `develop` on GitHub
- start "draft new release" on GitHub to ask it to "Generate release notes".  Make sure to choose the `develop` branch to get the full set of changes.: https://github.com/pgcentralfoundation/pgrx/releases/new
- paste them into the PR you made above
- edit them as best as you can while channeling @workingjubilee's spirit
- request a review
- do a squash merge into develop
- create the actual release on GitHub, tagging the `develop` branch with "${NEW_VERSION}", using the release notes you made in your PR

```shell
git switch develop
git pull origin/develop
./publish.sh
```
