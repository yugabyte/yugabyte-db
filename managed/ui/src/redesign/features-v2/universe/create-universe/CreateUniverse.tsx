/*
 * Created on Mon Mar 24 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useMemo, useRef } from 'react';
import { useMethods } from 'react-use';
import { useTranslation } from 'react-i18next';
import { styled } from '@material-ui/core';
import { YBMultiLevelStepper, mui, yba } from '@yugabyte-ui-library/core';
import { CreateUniverseBreadCrumb } from './CreateUniverseBreadCrumb';
import AuthenticatedArea from '@app/pages/AuthenticatedArea';
import SwitchCreateUniverseSteps from './SwitchCreateUniverseSteps';
import { getCreateUniverseSteps } from './CreateUniverseUtils';
import {
  CreateUniverseContext,
  createUniverseFormMethods,
  createUniverseFormProps,
  initialCreateUniverseFormState,
  StepsRef
} from './CreateUniverseContext';
//style imports
import './styles/override.css';

import YBLogo from '../../../assets/yb_logo.svg';
import Close from '../../../assets/close rounded.svg';

const { YBButton } = yba;

const { Grid2: Grid, Typography, Box } = mui;

const CreateUniverseRoot = styled('div')(() => ({
  '& .full-height-container': {
    backgroundColor: '#fff !important',
    display: 'flex',
    height: '100vh',
    width: '100vw',
    flexDirection: 'column'
  }
}));

const CreateHeader = styled('div')(() => ({
  display: 'flex',
  flexDirection: 'row',
  padding: '8px 24px 8px 20px',
  height: '64px',
  backgroundColor: '#1E154B',
  alignItems: 'center',
  justifyContent: 'space-between',
  minWidth: '1200px'
}));

export function CreateUniverse() {
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });
  const restoreContextData = useMethods(createUniverseFormMethods, initialCreateUniverseFormState);
  const [{ activeStep, resilienceType }] = restoreContextData;
  const steps = useMemo(() => getCreateUniverseSteps(t, resilienceType), [t, resilienceType]);
  const currentStepRef = useRef<StepsRef>(null);

  return (
    <CreateUniverseRoot>
      <AuthenticatedArea simpleMode>
        <CreateUniverseContext.Provider
          value={([...restoreContextData, {}] as unknown) as createUniverseFormProps}
        >
          {/* Header component */}
          <CreateHeader>
            <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px', width: '100%' }}>
              <YBLogo />
              <Typography
                variant="h4"
                sx={{ color: '#FFFFFF', fontSize: '18px', fontWeight: 600, lineHeight: '24px' }}
              >
                {t('title', { keyPrefix: 'createUniverseV2' })}
              </Typography>
            </Box>
            <Close style={{ cursor: 'pointer' }} />
          </CreateHeader>
          {/* Body */}
          <Grid container spacing={{ xs: 3, md: 3, lg: 3, xl: 6 }} minHeight={'100%'}>
            <Grid sx={{ borderRight: '1px solid #E9EEF2', height: '100vh' }} size="auto">
              <YBMultiLevelStepper dataTestId="stepper" activeStep={activeStep} steps={steps} />
            </Grid>
            <Grid container direction={'column'} size="grow" spacing={0}>
              <Grid size="auto">
                <CreateUniverseBreadCrumb />
              </Grid>
              <Grid container>
                <Box
                  sx={{
                    display: 'flex',
                    flexDirection: 'column',
                    maxWidth: '1024px',
                    minWidth: '856px',
                    gap: 3,
                    mr: 1
                  }}
                >
                  <SwitchCreateUniverseSteps ref={currentStepRef} />
                  {/* Footer */}
                  <Grid
                    container
                    display="flex"
                    alignItems="center"
                    justifyContent="space-between"
                    direction="row"
                    sx={{ mb: 3 }}
                  >
                    <YBButton
                      variant="secondary"
                      size="large"
                      dataTestId="create-universe-cancel-button"
                    >
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
                        dataTestId="create-universe-back-button"
                      >
                        {t('back', { keyPrefix: 'common' })}
                      </YBButton>
                      <YBButton
                        onClick={() => {
                          currentStepRef.current?.onNext();
                        }}
                        variant="ybaPrimary"
                        size="large"
                        dataTestId="create-universe-next-button"
                      >
                        {t(activeStep === 9 ? 'create' : 'next', { keyPrefix: 'common' })}
                      </YBButton>
                    </Grid>
                  </Grid>
                </Box>
              </Grid>
            </Grid>
          </Grid>
        </CreateUniverseContext.Provider>
      </AuthenticatedArea>
    </CreateUniverseRoot>
  );
}
