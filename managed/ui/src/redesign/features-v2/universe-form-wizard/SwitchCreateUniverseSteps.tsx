/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle, useRef } from 'react';
import { useMap } from 'react-use';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  CreateUniverseSteps,
  StepsRef
} from './CreateUniverseContext';
import { GeneralSettings } from './steps/general-settings';
import { ResilienceAndRegions } from './steps/resilence-regions/ResilienceAndRegions';

const SwitchCreateUniverseSteps = forwardRef((_props, forwardRef) => {
  const [{ activeStep }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const currentComponentRef = useRef<StepsRef>(null);
  const [, { get }] = useMap<Record<number, JSX.Element>>({
    [CreateUniverseSteps.GENERAL_SETTINGS]: <GeneralSettings ref={currentComponentRef} />,
    [CreateUniverseSteps.RESILIENCE_AND_REGIONS]: <ResilienceAndRegions ref={currentComponentRef} />
  });
  useImperativeHandle(forwardRef, () => currentComponentRef.current, [
    currentComponentRef.current,
    activeStep
  ]);

  const getCurrentComponent = () => {
    return get(activeStep);
  };

  return getCurrentComponent() ?? null;
});

SwitchCreateUniverseSteps.displayName = 'SwitchCreateUniverseSteps';

export default SwitchCreateUniverseSteps;
