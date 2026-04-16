import { ReleaseArtifacts, ReleaseState, ReleasePlatformArchitecture, ReleaseType, ReleasePlatform } from "../components/dtos";

export const getImportedArchitectures = (artifacts: ReleaseArtifacts[]) => {
  const architectures: any[] = [];
  artifacts?.forEach((artifact: ReleaseArtifacts) => {
    if (artifact.architecture === ReleasePlatformArchitecture.ARM) {
      architectures.push(ReleasePlatformArchitecture.ARM);
    } else if (artifact.architecture === ReleasePlatformArchitecture.X86) {
      architectures.push(ReleasePlatformArchitecture.X86);
    } else if (artifact.platform === ReleasePlatform.KUBERNETES) {
      architectures.push(null);
    }
  });
  return architectures;
};

export const getDeploymentStatus = (deploymentStatus: ReleaseState) => {
  if (deploymentStatus === ReleaseState.ACTIVE) {
    return 'active';
  } else if (deploymentStatus === ReleaseState.DISABLED) {
    return 'disabled';
  } else if (deploymentStatus === ReleaseState.INCOMPLETE) {
    return 'Incomplete';
  }
  return 'disabled';
};

export const MAX_RELEASE_TAG_CHAR = 10;
export const MAX_RELEASE_VERSION_CHAR = 30;
