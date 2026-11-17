import { forwardRef, useContext } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import {
  ENABLE_MULTI_PROVIDER_CREATE,
  isMultiProvider
} from '../../helpers/providerConfigurations';
import { InstanceSettingsForm, InstanceSettingsProps } from './InstanceSettings';
import { MultiProviderInstanceSettings } from './MultiProviderInstanceSettings';

/** Create-universe step entry: multi-provider UI vs existing single-provider form. */
export const InstanceSettingsStep = forwardRef<StepsRef, InstanceSettingsProps>((props, ref) => {
  const [{ generalSettings }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;

  if (
    !props.embedded &&
    !props.editMode &&
    ENABLE_MULTI_PROVIDER_CREATE &&
    isMultiProvider(generalSettings)
  ) {
    return <MultiProviderInstanceSettings ref={ref} />;
  }

  return <InstanceSettingsForm ref={ref} {...props} />;
});

InstanceSettingsStep.displayName = 'InstanceSettingsStep';
