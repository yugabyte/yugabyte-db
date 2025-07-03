/*
 * Created on Mon Mar 24 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useMemo, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { useMethods } from 'react-use';
import { YBMultiLevelStepper, mui, yba } from '@yugabyte-ui-library/core';
import { getCreateUniverseSteps } from './CreateUniverseUtils';
import {
  CreateUniverseContext,
  createUniverseFormMethods,
  createUniverseFormProps,
  initialCreateUniverseFormState,
  StepsRef
} from './CreateUniverseContext';
import SwitchCreateUniverseSteps from './SwitchCreateUniverseSteps';
import { CreateUniverseBreadCrumb } from './CreateUniverseBreadCrumb';

import { ReactComponent as YBLogo } from '../../assets/yb_logo.svg';
import { ReactComponent as Close } from '../../assets/close rounded.svg';

const { YBButton } = yba;

const { Grid2: Grid, Typography } = mui;

export const CreateUniverse = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const steps = useMemo(() => getCreateUniverseSteps(t), [t]);
  const restoreContextData = useMethods(createUniverseFormMethods, initialCreateUniverseFormState);
  const [{ activeStep }] = restoreContextData;
  const currentStepRef = useRef<StepsRef>(null);
  return (
    <CreateUniverseContext.Provider
      value={([...restoreContextData, {}] as unknown) as createUniverseFormProps}
    >
      <Grid
        container
        sx={{
          backgroundColor: '#1E154B',
          height: '64px',
          padding: '8px 24px',
          alignItems: 'center',
          justifyContent: 'space-between'
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <YBLogo />
          <Typography
            variant="h4"
            sx={{ color: '#FFFFFF', fontSize: '18px', fontWeight: 600, marginLeft: '16px' }}
          >
            {t('title', { keyPrefix: 'createUniverseV2' })}
          </Typography>
        </div>
        <Close style={{ cursor: 'pointer' }} />
      </Grid>
      <Grid container spacing={2}>
        <Grid sx={{ borderRight: '1px solid #E9EEF2', height: '100vh' }}>
          <YBMultiLevelStepper activeStep={activeStep} steps={steps} />
        </Grid>
        <Grid
          container
          direction="column"
          size="grow"
          sx={{ padding: '16px', maxWidth: '1024px', minWidth: '856px', gap: 0 }}
        >
          <Grid container sx={{ padding: '20px 0px' }}>
            <CreateUniverseBreadCrumb />
          </Grid>
          <Grid container sx={{ height: '32px' }}></Grid>
          <SwitchCreateUniverseSteps ref={currentStepRef} />
          <Grid
            container
            alignItems="center"
            justifyContent="space-between"
            direction="row"
            sx={{ marginTop: '32px' }}
          >
            <YBButton variant="secondary" size="large">
              {t('cancel', { keyPrefix: 'common' })}
            </YBButton>
            <Grid container alignItems="center" justifyContent="flex-end" spacing={2}>
              <YBButton
                onClick={() => {
                  currentStepRef.current?.onPrev();
                }}
                disabled={activeStep === 1}
                variant="secondary"
                size="large"
              >
                {t('back', { keyPrefix: 'common' })}
              </YBButton>
              <YBButton
                onClick={() => {
                  currentStepRef.current?.onNext();
                }}
                variant="ybaPrimary"
                size="large"
              >
                {t(activeStep === 9 ? 'create' :'next', { keyPrefix: 'common' })}
              </YBButton>
            </Grid>
          </Grid>
        </Grid>
      </Grid>
    </CreateUniverseContext.Provider>
  );
};
