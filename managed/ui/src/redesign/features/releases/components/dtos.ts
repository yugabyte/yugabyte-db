export interface Releases {
  uuid?: string;
  release_tag: string;
  schema?: string;
  version: string;
  yb_type: string;
  artifacts: ReleaseArtifacts[];
  release_type: ReleaseType | string,
  release_date: string;
  in_use: boolean;
  release_notes: string;
  state: ReleaseState;
  universes: ReleaseUniverses[];
}

export interface ReleaseArtifacts {
  location: ReleaseArtifactLocation
  sha256: string;
  platform: ReleasePlatform;
  architecture: ReleasePlatformArchitecture | string;
  signature: string;
}

export interface ReleaseArtifactLocation {
  fileID?: string;
  package_file_path?: string;
  signature_file_path?: string;
  package_url?: string;
  signature_url?: string;
}

export enum ReleasePlatform {
  LINUX = 'linux',
  KUBERNETES = 'kubernetes'
}

export enum ReleasePlatformArchitecture {
  X86 = 'x86_64',
  ARM = 'aarch64',
  KUBERNETES = 'kubernetes'
}

export enum ReleaseType {
  ALL = 'All',
  STS = 'STS',
  LTS = 'LTS',
  PREVIEW = 'Preview'
}

export enum ReleaseState {
  ACTIVE =  'ACTIVE',
  DISABLED = 'DISABLED',
  DELETED = 'DELETED'
}

export interface ReleaseUniverses {
  name: string;
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
  release_date?: string;
  release_type?: string;
  release_notes?:string;
}

export interface ReleaseFormFields {
  importMethod: string | AddReleaseImportMethod;
  installationPackageUrl?: string;
  signatureUrl?: string;
  installationPackageFile?: File | undefined | string;
  signatureFile?: File | undefined | string;
  releaseTag?: string,
  version?: string,
  architecture?: string,
  platform?: string,
  releaseDate?: string;
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
