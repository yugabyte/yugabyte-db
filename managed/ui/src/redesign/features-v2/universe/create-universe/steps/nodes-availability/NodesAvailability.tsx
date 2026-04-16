import { forwardRef } from 'react';
import { FormProvider } from 'react-hook-form';
import { ResilienceFormMode } from '../resilence-regions/dtos';
import { StepsRef } from '../../CreateUniverseContext';
import { NodesAvailabilityGuidedBody } from './NodesAvailabilityGuidedBody';
import { NodesAvailabilityExpertBody } from './NodesAvailabilityExpertBody';
import { useNodesAvailabilityStep } from './useNodesAvailabilityStep';

export type NodesAvailabilityProps = object;

export const NodesAvailability = forwardRef<StepsRef, NodesAvailabilityProps>(
  function NodesAvailability(_props, ref) {
  const step = useNodesAvailabilityStep(ref);
  const mode = step.resilienceAndRegionsSettings?.resilienceFormMode ?? ResilienceFormMode.GUIDED;

  return (
    <FormProvider {...step.methods}>
      {mode === ResilienceFormMode.EXPERT_MODE ? (
        <NodesAvailabilityExpertBody
          regions={step.regions}
          icon={step.icon}
          showErrorsAfterSubmit={step.showErrorsAfterSubmit}
          lesserNodesTransValues={step.lesserNodesTransValues}
          errors={step.errors}
          t={step.t}
          inferredResilience={step.inferredResilience}
          effectiveReplicationFactor={step.effectiveReplicationFactor}
          resilienceAndRegionsSettings={step.resilienceAndRegionsSettings}
        />
      ) : (
        <NodesAvailabilityGuidedBody
          regions={step.regions}
          icon={step.icon}
          showErrorsAfterSubmit={step.showErrorsAfterSubmit}
          lesserNodesTransValues={step.lesserNodesTransValues}
          errors={step.errors}
          t={step.t}
          resilienceAndRegionsSettings={step.resilienceAndRegionsSettings}
        />
      )}
    </FormProvider>
  );
  }
);

NodesAvailability.displayName = 'NodesAvailability';
