Publishing a new Release
===============================================================================

* [ ] Check that **all** CI jobs run without errors on the `master` branch

* [ ] Close all remaining issues on the current milestone

* [ ] Update the [Changelog]

* [ ] Write the announcement in [NEWS.md]

* [ ] Rebuild the docker image and upload it (`make docker_image docker_push`)

* [ ] Upload the zipball to PGXN

* [ ] Check the PGXN install process

* [ ] Close the current milsetone and open the next one

* [ ] Rebase the `stable` branch from `master`

* [ ] Tag the master branch

* [ ] Open a ticket to the [PostgreSQL YUM repository project]

* [ ] Bump to the new version number in [anon.control] and [META.json]

* [ ] Publish the announcement

[Changelog]: CHANGELOG.md
[NEWS.md]: NEWS.md
[anon.control]: anon.control
[META.json]: META.json
[PostgreSQL YUM repository project]: https://redmine.postgresql.org/projects/pgrpms/issues
