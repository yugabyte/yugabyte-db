import { ConfirmBootstrapStep } from './ConfirmBootstrapStep';
import { FormStep } from './EditConfigTargetModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  sourceUniverseUUID: string;
  storageConfigUuid: string;
}

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  sourceUniverseUUID,
  storageConfigUuid
}: CurrentFormStepProps) => {
  switch (currentFormStep) {
    case FormStep.SELECT_TARGET_UNIVERSE:
      return (
        <SelectTargetUniverseStep
          sourceUniverseUuid={sourceUniverseUUID}
          isFormDisabled={isFormDisabled}
        />
      );
    case FormStep.CONFIGURE_BOOTSTRAP:
      return <ConfirmBootstrapStep storageConfigUuid={storageConfigUuid} />;
  }
};
