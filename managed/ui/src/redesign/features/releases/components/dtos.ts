export interface Releases {
  release_uuid?: string;
  release_tag: string;
  schema?: string;
  version: string;
  yb_type: string;
  artifacts: ReleaseArtifacts[];
  release_type: ReleaseType | string,
  release_date_msecs: number;
  release_notes: string;
  state: ReleaseState;
  universes: ReleaseUniverses[];
}

export interface ReleaseArtifacts {
  package_file_id?: string;
  file_name?: string | any;
  signature_file_path?: string | any;
  package_url?: string;
  signature_url?: string;
  sha256: string;
  platform: ReleasePlatform;
  architecture: ReleasePlatformArchitecture | string | null;
  signature: string;
}

export enum ReleasePlatform {
  LINUX = 'LINUX',
  KUBERNETES = 'KUBERNETES'
}

export enum ReleaseYBType {
  YUGABYTEDB = 'YBDB',
  YUGAWARE = 'YUGAWARE'
}

export enum ReleasePlatformArchitecture {
  X86 = 'x86_64',
  ARM = 'aarch64'
}

export enum ReleaseType {
  ALL = 'All',
  STS = 'STS',
  LTS = 'LTS',
  PREVIEW_DEFAULT = 'PREVIEW (DEFAULT)',
  PREVIEW = 'PREVIEW'
}

export enum ReleaseState {
  ACTIVE =  'ACTIVE',
  DISABLED = 'DISABLED',
  DELETED = 'DELETED',
  INCOMPLETE = 'INCOMPLETE'
}

export enum UrlArtifactStatus {
  EMPTY = '',
  WAITING = 'waiting',
  SUCCESS =  'success',
  RUNNING = 'running',
  FAILURE = 'failure'
}

export interface ReleaseUniverses {
  name: string;
  uuid: string;
  creation_date: string;
}

export interface UUID {
  uuid: string;
}

export interface ReleaseArtifactId {
  artifactID: string;
}

export interface ReleaseSpecificArtifact {
  uuid: string;
  version: string;
  yb_type: string;
  sha256: string;
  platform: ReleasePlatform;
  architecture: ReleasePlatformArchitecture | string;
  signature: string;
  release_date_msecs?: number;
  release_type?: string;
  release_notes?:string;
}

export interface ReleaseFormFields {
  importMethod: string | AddReleaseImportMethod;
  installationPackageUrl?: string;
  ybType?: string;
  sha256?: string;
  signatureUrl?: string;
  installationPackageFile?: File | undefined;
  signatureFile?: File | undefined;
  releaseTag?: string,
  version?: string,
  architecture?: string | null,
  platform?: string,
  releaseDate?: number;
  releaseNotes? : string;
  releaseType?: ReleaseType | string;
}

export enum AddReleaseImportMethod {
  FILE_UPLOAD = 'File_Upload',
  URL = 'URL'
}

export interface EditReleaseTagFormFields {
  releaseTag: string;
};

export const ModalTitle = {
  ADD_RELEASE:  'Import Database Release',
  ADD_ARCHITECTURE: 'New Architecture',
  EDIT_KUBERNETES: 'Edit Kubernetes',
  EDIT_X86: 'Edit VM x86',
  EDIT_AARCH: 'Edit VM ARM',
  EDIT_RELEASE_TAG: 'Edit Release Tag'
} as const;

export interface ReleasePlatformButtonProps {
  label: string;
  value: ReleasePlatform;
}

export interface ReleaseArchitectureButtonProps {
  label: string;
  value: ReleasePlatformArchitecture;
}
