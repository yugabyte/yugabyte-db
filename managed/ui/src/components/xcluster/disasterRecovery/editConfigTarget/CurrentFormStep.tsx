import { ConfirmBootstrapStep } from './ConfirmBootstrapStep';
import { FormStep } from './EditConfigTargetModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  storageConfigUuid: string;
  sourceUniverseUuid: string;
  targetUniverseUuid: string;
}

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  storageConfigUuid,
  sourceUniverseUuid,
  targetUniverseUuid
}: CurrentFormStepProps) => {
  switch (currentFormStep) {
    case FormStep.SELECT_TARGET_UNIVERSE:
      return (
        <SelectTargetUniverseStep
          sourceUniverseUuid={sourceUniverseUuid}
          targetUniverseUuid={targetUniverseUuid}
          isFormDisabled={isFormDisabled}
        />
      );
    case FormStep.CONFIGURE_BOOTSTRAP:
      return <ConfirmBootstrapStep storageConfigUuid={storageConfigUuid} />;
  }
};
