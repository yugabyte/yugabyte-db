
* [ ] Check that **all** CI jobs run without errors on the `latest` branch
* [ ] Close all remaining issues on the current milestone (if it's a major version)
* [ ] Close the current milestone and open the next one (if it's a major version)
* [ ] Create a Release MR
* [ ] Run `cargo audit --fix` to update dependencies
* [ ] Update the [Changelog]
* [ ] Update the [AUTHORS.md] list
* [ ] Write the announcement in [NEWS.md] (if it's a major version)
* [ ] Merge the Release MR
* [ ] Tag the `latest` branch
* [ ] The tag will trigger a [tag pipeline]. Wait for it end (approx. 90 min !)
* [ ] Check that a new [release] was created
* [ ] Rebase the `stable` branch from `latest`
* [ ] Rebuild the docker image `latest` and upload it
      (`make docker_image docker_push`)
* [ ] Rebuild the docker image `stable` and upload it
      (`DOCKER_TAG=stable make docker_image docker_push`)
* [ ] Build the docker image `x.y.z` and upload it
      (`DOCKER_TAG=x.y.z make docker_image docker_push`)
* [ ] Update the upstream repositories
* [ ] Bump to the new version number in [Cargo.toml]
* [ ] Publish the announcement

[AUTHORS.md]: AUTHORS.md
[Changelog]: CHANGELOG.md
[NEWS.md]: NEWS.md
[Cargo.toml]: Cargo.toml
[Tags page]: https://gitlab.com/dalibo/postgresql_anonymizer/-/tags/
[tag pipeline]: https://gitlab.com/dalibo/postgresql_anonymizer/-/pipelines
[release]: https://gitlab.com/dalibo/postgresql_anonymizer/-/releases
