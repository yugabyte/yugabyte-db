import { ReleaseArtifacts, ReleaseState, ReleasePlatformArchitecture, ReleaseType, ReleasePlatform } from "../components/dtos";

export const getImportedArchitectures = (artifacts: ReleaseArtifacts[]) => {
  const architectures: string[] = [];
  artifacts?.forEach((artifact: ReleaseArtifacts) => {
    if (artifact.architecture === ReleasePlatformArchitecture.ARM) {
      architectures.push(ReleasePlatformArchitecture.ARM);
    } else if (artifact.architecture === ReleasePlatformArchitecture.X86) {
      architectures.push(ReleasePlatformArchitecture.X86);
    } else if (artifact.platform === ReleasePlatform.KUBERNETES) {
      architectures.push(ReleasePlatformArchitecture.KUBERNETES);
    }
  });
  return architectures;
};

export const getDeploymentStatus = (deploymentStatus: ReleaseState) => {
  if (deploymentStatus === ReleaseState.ACTIVE) {
    return 'active';
  } else if (deploymentStatus === ReleaseState.DISABLED) {
    return 'disabled';
  }
  return 'disabled';
};
