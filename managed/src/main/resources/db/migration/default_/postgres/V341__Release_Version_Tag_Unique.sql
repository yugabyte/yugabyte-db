
UPDATE release 
  SET release_tag='NULL-VALUE-DO-NOT-USE-AS-INPUT'
  WHERE release_tag is null;
ALTER TABLE release
  ADD CONSTRAINT version_tag_unique UNIQUE (version, release_tag);