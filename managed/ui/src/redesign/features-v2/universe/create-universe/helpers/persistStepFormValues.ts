import { useEffect, useRef } from 'react';
import type { FieldValues, UseFormGetValues, UseFormWatch } from 'react-hook-form';
import {
  createUniverseFormMethods,
  initialCreateUniverseFormState
} from '../CreateUniverseContext';

type CreateUniverseSavers = ReturnType<typeof createUniverseFormMethods>;

/**
 * Persist step form values into CreateUniverseContext on every change.
 * save is kept in a ref so useMethods recreating closures does not resubscribe/loop.
 * Uses getValues() for a full snapshot (watch callback values can be DeepPartial).
 */
export function usePersistStepFormValues<T extends FieldValues>(
  watch: UseFormWatch<T>,
  getValues: UseFormGetValues<T>,
  save: (data: T) => void
): void {
  const saveRef = useRef(save);
  saveRef.current = save;

  useEffect(() => {
    const subscription = watch(() => {
      saveRef.current(getValues());
    });
    return () => {
      // Flush on unmount so values set before the subscription attached (e.g. child
      // useEffectOnce) are still persisted when leaving the step.
      saveRef.current(getValues());
      subscription.unsubscribe();
    };
  }, [watch, getValues]);
}

export function resetProviderDependentSettings({
  saveResilienceAndRegionsSettings,
  saveNodesAvailabilitySettings,
  saveInstanceSettings
}: Pick<
  CreateUniverseSavers,
  | 'saveResilienceAndRegionsSettings'
  | 'saveNodesAvailabilitySettings'
  | 'saveInstanceSettings'
>): void {
  saveResilienceAndRegionsSettings(initialCreateUniverseFormState.resilienceAndRegionsSettings!);
  saveNodesAvailabilitySettings(initialCreateUniverseFormState.nodesAvailabilitySettings!);
  saveInstanceSettings(initialCreateUniverseFormState.instanceSettings!);
}
