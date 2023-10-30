import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { FormStep } from './EditConfigTargetModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  sourceUniverseUUID: string;
}

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  sourceUniverseUUID
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
      return <ConfigureBootstrapStep isFormDisabled={isFormDisabled} />;
  }
};
