create table if not exists release (
  release_uuid uuid primary key,
  version TEXT not null,
  release_tag TEXT,
  yb_type TEXT not null,
  release_type TEXT not null,
  state TEXT not null,
  release_date timestamp,
  release_notes TEXT
);

create table if not exists release_local_file (
  file_uuid uuid primary key,
  local_file_path TEXT
);

create table if not exists release_artifact (
  artifact_uuid uuid primary key,
  sha256 varchar(64),
  platform TEXT,
  architecture TEXT,
  signature TEXT,
  package_file_id uuid references release_local_file(file_uuid),
  package_url TEXT,
  s3_file TEXT,
  gcs_file TEXT,
  release uuid references release(release_uuid)
);