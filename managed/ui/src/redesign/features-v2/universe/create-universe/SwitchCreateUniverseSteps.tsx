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
  InstanceSettingsStep,
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
import { CloudType } from '@app/redesign/helpers/dtos';
import { getPrimaryProvider } from './helpers/providerConfigurations';

/**
 * @param resilieceType - The resilience type of the universe
 * @param currentComponentRef - The ref to the current component
 * @returns The steps mappings
 */
function getStepsMappings(
  resilieceType: ResilienceType,
  isK8s: boolean,
  currentComponentRef: React.RefObject<StepsRef>
) {
  // Single node has no nodes-availability step. Kubernetes has no proxy step.
  // Later steps shift so Next/Back stay on the visible stepper entries.
  const adjust = resilieceType === ResilienceType.SINGLE_NODE ? 1 : 0;
  const afterProxy = adjust + (isK8s ? 1 : 0);
  const mappings: Record<number, JSX.Element> = {
    [CreateUniverseSteps.GENERAL_SETTINGS]: <GeneralSettings ref={currentComponentRef} />,
    [CreateUniverseSteps.RESILIENCE_AND_REGIONS]: (
      <ResilienceAndRegions ref={currentComponentRef} />
    ),
    [CreateUniverseSteps.INSTANCE - adjust]: <InstanceSettingsStep ref={currentComponentRef} />,
    [CreateUniverseSteps.DATABASE - adjust]: <DatabaseSettings ref={currentComponentRef} />,
    [CreateUniverseSteps.SECURITY - adjust]: <SecuritySettings ref={currentComponentRef} />,
    [CreateUniverseSteps.ADVANCED_OTHER - afterProxy]: (
      <OtherAdvancedSettings ref={currentComponentRef} />
    ),
    [CreateUniverseSteps.REVIEW - afterProxy]: <ReviewAndSummary ref={currentComponentRef} />
  };
  if (!isK8s) {
    mappings[CreateUniverseSteps.ADVANCED_PROXY - adjust] = (
      <ProxySettings ref={currentComponentRef} />
    );
  }
  if (adjust === 0) {
    mappings[CreateUniverseSteps.NODES_AVAILABILITY] = (
      <NodesAvailability ref={currentComponentRef} />
    );
  }
  return mappings;
}

const SwitchCreateUniverseSteps = forwardRef((_props, forwardRef) => {
  const [{ activeStep, resilienceType, generalSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const isK8s =
    generalSettings?.cloud === CloudType.kubernetes ||
    getPrimaryProvider(generalSettings)?.code === CloudType.kubernetes;

  const currentComponentRef = useRef<StepsRef>(null);
  const [, { get, setAll }] = useMap<Record<number, JSX.Element>>(
    getStepsMappings(resilienceType!, isK8s, currentComponentRef)
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
      setAll(getStepsMappings(resilienceType!, isK8s, currentComponentRef));
    }
  }, [resilienceType, isK8s, activeStep]);

  const getCurrentComponent = () => {
    return get(activeStep);
  };

  return getCurrentComponent() ?? null;
});

SwitchCreateUniverseSteps.displayName = 'SwitchCreateUniverseSteps';

export default SwitchCreateUniverseSteps;
