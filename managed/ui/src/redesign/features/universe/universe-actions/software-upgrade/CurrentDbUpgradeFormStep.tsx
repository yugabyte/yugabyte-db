import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { ReleaseOption } from './types';
import { DbUpgradeFormStep } from './constants';
import { DbVersionStep } from './upgrade-steps/DbVersionStep';
import { UpgradeMethodStep } from './upgrade-steps/UpgradeMethodStep';

interface CurrentDbUpgradeFormStepProps {
  currentFormStep: DbUpgradeFormStep;
  currentRelease: string;
  targetReleaseOptions: ReleaseOption[];
  primaryBatchSize: number;
  currentUniverseUuid: string;
}

export const CurrentDbUpgradeFormStep = ({
  currentFormStep,
  currentRelease,
  targetReleaseOptions,
  primaryBatchSize,
  currentUniverseUuid
}: CurrentDbUpgradeFormStepProps) => {
  switch (currentFormStep) {
    case DbUpgradeFormStep.DB_VERSION:
      return (
        <DbVersionStep
          currentRelease={currentRelease}
          targetReleaseOptions={targetReleaseOptions}
          currentUniverseUuid={currentUniverseUuid}
        />
      );
    case DbUpgradeFormStep.UPGRADE_METHOD:
      return <UpgradeMethodStep primaryBatchSize={primaryBatchSize} />;
    case DbUpgradeFormStep.UPGRADE_PLAN:
      return <div>Not implemented</div>;
    case DbUpgradeFormStep.UPGRADE_PACE:
      return <div>Not implemented</div>;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
