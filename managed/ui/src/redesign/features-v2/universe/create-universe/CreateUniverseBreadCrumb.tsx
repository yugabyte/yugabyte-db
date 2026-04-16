/* eslint-disable no-console */
/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { CreateUniverseContext, CreateUniverseContextMethods } from './CreateUniverseContext';
import { getCreateUniverseSteps } from './CreateUniverseUtils';

export const CreateUniverseBreadCrumb = () => {
  const [{ activeStep, resilienceType }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const steps = useMemo(() => getCreateUniverseSteps(t, resilienceType), [t, resilienceType]);
  let totalStepCount = 0;
  let groupTitle = '';
  let subTitle = '';

  for (let i = 0; i < steps.length; i++) {
    if (totalStepCount + steps[i].subSteps.length < activeStep) {
      totalStepCount += steps[i].subSteps.length;
      continue;
    } else {
      groupTitle = steps[i].groupTitle;
      subTitle = steps[i].subSteps[activeStep - totalStepCount - 1].title;
      break;
    }
  }
  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        fontSize: '18px',
        fontWeight: '600',
        gap: '12px',
        padding: '40px 24px 40px 0px'
      }}
    >
      <span style={{ color: '#97A5B0' }}>{groupTitle}</span>
      <span style={{ color: '#97A5B0' }}>/</span>
      <span>{subTitle}</span>
    </div>
  );
};
