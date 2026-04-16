/**
 * Release state from the backend. Used in ResponseRelease.state.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/Release.java (ReleaseState enum)
 */
export const ReleaseState = {
  ACTIVE: 'ACTIVE',
  DISABLED: 'DISABLED',
  INCOMPLETE: 'INCOMPLETE',
  DELETED: 'DELETED'
} as const;
export type ReleaseState = typeof ReleaseState[keyof typeof ReleaseState];

/**
 * Source: managed/src/main/java/com/yugabyte/yw/models/Release.java (YbType enum)
 */
export const ReleaseYbType = {
  YBDB: 'YBDB'
} as const;
export type ReleaseYbType = typeof ReleaseYbType[keyof typeof ReleaseYbType];

/**
 * Shape of a single release. Used in GET /api/v1/customers/{cUUID}/ybdb_release list response.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/controllers/apiModels/ResponseRelease.java
 */
export interface YbdbRelease {
  release_uuid: string;
  version: string;
  yb_type: ReleaseYbType;
  release_type: 'LTS' | 'STS' | 'PREVIEW';
  state: ReleaseState;
  artifacts?: YbdbReleaseArtifact[];
  release_date_msecs?: number | null;
  release_notes?: string | null;
  release_tag?: string | null;
  universes?: YbdbReleaseUniverse[];
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/controllers/apiModels/ResponseRelease.java (Artifact inner class)
 */
export interface YbdbReleaseArtifact {
  package_file_id?: string;
  file_name?: string;
  package_url?: string;
  platform: 'LINUX' | 'KUBERNETES';
  architecture: 'x86_64' | 'aarch64';
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/controllers/apiModels/ResponseRelease.java (Universe inner class)
 */
export interface YbdbReleaseUniverse {
  uuid: string;
  name: string;
  creation_date: string;
}
