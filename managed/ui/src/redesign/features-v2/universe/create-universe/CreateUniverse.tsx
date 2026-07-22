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
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { YBMultiLevelStepper, mui, yba } from '@yugabyte-ui-library/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { CreateUniverseBreadCrumb } from './CreateUniverseBreadCrumb';
import AuthenticatedArea from '@app/pages/AuthenticatedArea';
import SwitchCreateUniverseSteps from './SwitchCreateUniverseSteps';
import { getCreateUniverseSteps } from './CreateUniverseUtils';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import {
  CreateUniverseContext,
  createUniverseFormMethods,
  createUniverseFormProps,
  initialCreateUniverseFormState,
  StepsRef
} from './CreateUniverseContext';
import { ResilienceType } from './steps/resilence-regions/dtos';
import { CloudType } from '@app/redesign/helpers/dtos';
//style imports
import './styles/override.css';

import YBLogo from '../../../assets/yb_logo.svg';
import Close from '../../../assets/close rounded.svg';

const { YBButton } = yba;

const { Grid2: Grid, Typography, Box, styled } = mui;

const CreateUniverseRoot = styled('div')(() => ({
  '& .full-height-container': {
    backgroundColor: '#fff !important',
    display: 'flex',
    height: '100vh',
    width: '100%',
    flexDirection: 'column',
    position: 'relative',
    overflow: 'hidden'
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
  const [{ activeStep, resilienceType, generalSettings }] = restoreContextData;
  const isK8s =
    generalSettings?.cloud === CloudType.kubernetes ||
    generalSettings?.providerConfiguration?.code === CloudType.kubernetes;
  const steps = useMemo(() => getCreateUniverseSteps(t, resilienceType, isK8s, false), [
    t,
    resilienceType,
    isK8s
  ]);
  const currentStepRef = useRef<StepsRef>(null);

  //To speed up the interaction
  const { isLoading } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList);

  const getButtonLabel = (resType: ResilienceType | undefined, actStep: number) => {
    if (resType === ResilienceType.SINGLE_NODE) {
      if (actStep === 8) return t('applyChanges', { keyPrefix: 'common' });
      else return t(actStep === 7 ? 'reviewAndCreate' : 'next', { keyPrefix: 'common' });
    } else {
      if (actStep === 9) return t('applyChanges', { keyPrefix: 'common' });
      else return t(actStep === 8 ? 'reviewAndCreate' : 'next', { keyPrefix: 'common' });
    }
  };

  if (isLoading) return <YBLoadingCircleIcon />;

  return (
    <CreateUniverseRoot className="create-universe-root">
      <AuthenticatedArea simpleMode>
        <CreateUniverseContext.Provider
          value={[...restoreContextData, {}] as unknown as createUniverseFormProps}
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
            <Close
              style={{ cursor: 'pointer' }}
              onClick={() => {
                window.location.href = '/';
              }}
            />
          </CreateHeader>
          {/* Body */}
          <Box sx={{ flex: 1, minHeight: 0, display: 'flex', overflow: 'hidden' }}>
            <Grid
              container
              spacing={{ xs: 3, md: 3, lg: 3, xl: 6 }}
              sx={{ flex: 1, minHeight: 0, width: '100%', flexWrap: 'nowrap' }}
            >
              <Grid
                sx={{ borderRight: '1px solid #E9EEF2', overflowY: 'auto', flexShrink: 0 }}
                size="auto"
              >
                <YBMultiLevelStepper dataTestId="stepper" activeStep={activeStep} steps={steps} />
              </Grid>
              <Grid
                container
                direction={'column'}
                size="grow"
                spacing={0}
                sx={{ flex: 1, minHeight: 0, minWidth: 0 }}
              >
                <Grid size="auto">
                  <CreateUniverseBreadCrumb />
                </Grid>
                <Grid size="grow" sx={{ minHeight: 0, overflowY: 'auto' }}>
                  <Box
                    sx={{
                      display: 'flex',
                      flexDirection: 'column',
                      maxWidth: '1024px',
                      minWidth: '856px',
                      width: '100%',
                      gap: 3,
                      mr: 1,
                      pb: 3
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
                    >
                    <YBButton
                      variant="secondary"
                      size="large"
                      dataTestId="create-universe-cancel-button"
                      onClick={() => {
                        window.location.href = '/';
                      }}
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
                        {getButtonLabel(resilienceType, activeStep)}
                      </YBButton>
                    </Grid>
                    </Grid>
                  </Box>
                </Grid>
              </Grid>
            </Grid>
          </Box>
        </CreateUniverseContext.Provider>
      </AuthenticatedArea>
    </CreateUniverseRoot>
  );
}
