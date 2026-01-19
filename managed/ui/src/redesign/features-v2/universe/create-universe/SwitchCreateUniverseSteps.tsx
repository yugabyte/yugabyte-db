/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle, useRef } from 'react';
import { useMap } from 'react-use';
import { browserHistory } from 'react-router';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  CreateUniverseSteps,
  StepsRef
} from './CreateUniverseContext';
import {
  DatabaseSettings,
  GeneralSettings,
  InstanceSettings,
  NodesAvailability,
  ResilienceAndRegions,
  SecuritySettings,
  OtherAdvancedSettings,
  ProxySettings,
  ReviewAndSummary
} from './steps';
import { hasNecessaryPerm } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { ResilienceType } from './steps/resilence-regions/dtos';

/**
 * @param resilieceType - The resilience type of the universe
 * @param currentComponentRef - The ref to the current component
 * @returns The steps mappings
 */
function getStepsMappings(
  resilieceType: ResilienceType,
  currentComponentRef: React.RefObject<StepsRef>
) {
  // If the resilience type is single node, we need to adjust the steps by 1
  // because the single node does not have the nodes availability step
  const adjust = resilieceType === ResilienceType.SINGLE_NODE ? 1 : 0;
  const mappings = {
    [CreateUniverseSteps.GENERAL_SETTINGS]: <GeneralSettings ref={currentComponentRef} />,
    [CreateUniverseSteps.RESILIENCE_AND_REGIONS]: (
      <ResilienceAndRegions ref={currentComponentRef} />
    ),
    [CreateUniverseSteps.INSTANCE - adjust]: <InstanceSettings ref={currentComponentRef} />,
    [CreateUniverseSteps.DATABASE - adjust]: <DatabaseSettings ref={currentComponentRef} />,
    [CreateUniverseSteps.SECURITY - adjust]: <SecuritySettings ref={currentComponentRef} />,
    [CreateUniverseSteps.ADVANCED_PROXY - adjust]: <ProxySettings ref={currentComponentRef} />,
    [CreateUniverseSteps.ADVANCED_OTHER - adjust]: (
      <OtherAdvancedSettings ref={currentComponentRef} />
    ),
    [CreateUniverseSteps.REVIEW - adjust]: <ReviewAndSummary ref={currentComponentRef} />
  };
  if (adjust === 0) {
    mappings[CreateUniverseSteps.NODES_AVAILABILITY] = (
      <NodesAvailability ref={currentComponentRef} />
    );
  }
  return mappings;
}

const SwitchCreateUniverseSteps = forwardRef((_props, forwardRef) => {
  const [{ activeStep, resilienceType }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const currentComponentRef = useRef<StepsRef>(null);
  const [, { get, setAll }] = useMap<Record<number, JSX.Element>>(
    getStepsMappings(resilienceType!, currentComponentRef)
  );
  useImperativeHandle(forwardRef, () => currentComponentRef.current, [
    currentComponentRef.current,
    activeStep
  ]);

  // Check for create universe permission and redirect to home if not available
  useEffect(() => {
    const hasCreateUniversePermission = hasNecessaryPerm(ApiPermissionMap.CREATE_UNIVERSE);
    if (!hasCreateUniversePermission) {
      browserHistory.push('/');
    }
  }, []);

  useEffect(() => {
    if (activeStep === CreateUniverseSteps.RESILIENCE_AND_REGIONS) {
      setAll(getStepsMappings(resilienceType!, currentComponentRef));
    }
  }, [resilienceType, activeStep]);

  const getCurrentComponent = () => {
    return get(activeStep);
  };

  return getCurrentComponent() ?? null;
});

SwitchCreateUniverseSteps.displayName = 'SwitchCreateUniverseSteps';

export default SwitchCreateUniverseSteps;
