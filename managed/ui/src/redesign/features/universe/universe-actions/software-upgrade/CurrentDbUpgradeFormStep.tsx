import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { ReleaseOption } from './types';
import { DbUpgradeFormStep } from './constants';
import { DbVersionStep } from './upgrade-steps/DbVersionStep';
import { UpgradeMethodStep } from './upgrade-steps/UpgradeMethodStep';
import { UpgradePlanStep } from './upgrade-steps/UpgradePlanStep';
import { UpgradePaceStep } from './upgrade-steps/UpgradePaceStep';

interface CurrentDbUpgradeFormStepProps {
  currentFormStep: DbUpgradeFormStep;
  currentRelease: string;
  targetReleaseOptions: ReleaseOption[];
  maxNodesPerBatchMaximum: number;
  currentUniverseUuid: string;
}

export const CurrentDbUpgradeFormStep = ({
  currentFormStep,
  currentRelease,
  targetReleaseOptions,
  maxNodesPerBatchMaximum,
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
      return <UpgradeMethodStep maxNodesPerBatchMaximum={maxNodesPerBatchMaximum} />;
    case DbUpgradeFormStep.UPGRADE_PLAN:
      return <UpgradePlanStep />;
    case DbUpgradeFormStep.UPGRADE_PACE:
      return <UpgradePaceStep maxNodesPerBatchMaximum={maxNodesPerBatchMaximum} />;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
