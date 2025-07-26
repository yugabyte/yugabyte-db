/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { styled } from '@material-ui/core';
import { yupResolver } from '@hookform/resolvers/yup';
import { FormProvider, useForm } from 'react-hook-form';
import { AlertVariant, mui, YBAlert, YBButton, YBTooltip } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { ResilienceAndRegionsProps, ResilienceFormMode, ResilienceType } from './dtos';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { ResilienceTypeField } from '../../fields/resilience-type/ResilienceType';

import { GuidedMode } from './GuidedMode';
import { FreeFormMode } from './FreeFormMode';
import { RegionSelection } from './RegionSelection';
import {
  FAULT_TOLERANCE_TYPE,
  REGIONS_FIELD,
  REPLICATION_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../fields/FieldNames';

import { ResilienceAndRegionsSchema } from './ValidationSchema';
import { getFaultToleranceNeeded, getFaultToleranceNeededForAZ } from '../../CreateUniverseUtils';
import { ReactComponent as DocTick } from '../../../../assets/doc_tick.svg';
import { ReactComponent as DocTickUnSelected } from '../../../../assets/doc_tick_unselected.svg';
import { ReactComponent as Flash } from '../../../../assets/flash_transparent.svg';

const { Grid2: Grid, ButtonGroup } = mui;

const StyledHelpText = styled('div')(({ theme }) => ({
  padding: '16px 24px',
  display: 'flex',
  gap: '8px',
  color: theme.palette.grey[700],
  fontSize: '13px',
  fontWeight: 400,
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  marginTop: '24px',
  '& > a': {
    color: theme.palette.grey[700],
    textDecoration: 'underline',
    cursor: 'pointer'
  }
}));

export const ResilienceAndRegions = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { resilienceAndRegionsSettings },
    { moveToPreviousPage, saveResilienceAndRegionsSettings, moveToNextPage, setResilienceType }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions'
  });

  const methods = useForm<ResilienceAndRegionsProps>({
    defaultValues: resilienceAndRegionsSettings,
    resolver: yupResolver(ResilienceAndRegionsSchema(t))
  });

  const { watch, trigger } = methods;

  const formMode = watch(RESILIENCE_FORM_MODE);
  const regions = watch(REGIONS_FIELD);
  const replicationFactor = watch(REPLICATION_FACTOR);
  const faultToleranceType = watch(FAULT_TOLERANCE_TYPE);
  const faultToleranceForRegion = getFaultToleranceNeeded(replicationFactor);
  const faultToleranceforAz = getFaultToleranceNeededForAZ(replicationFactor);
  const resilienceType = watch(RESILIENCE_TYPE);

  const availabilityZoneCount = regions.reduce((acc, region) => {
    return acc + region.zones.length;
  }, 0);

  const { errors } = methods.formState;

  useEffect(() => {
    trigger(FAULT_TOLERANCE_TYPE);
  }, [regions, replicationFactor, faultToleranceType, formMode, resilienceType]);

  useEffect(() => {
    setResilienceType(resilienceType);
  }, [resilienceType]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          saveResilienceAndRegionsSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <ResilienceTypeField<ResilienceAndRegionsProps> name="resilienceType" />
      <div style={{ marginBottom: '16px' }} />
      {resilienceType === ResilienceType.REGULAR && (
        <StyledPanel>
          <StyledHeader>
            <Grid alignContent={'center'} justifyContent={'space-between'} container>
              {t('title')}
              <ButtonGroup>
                <YBTooltip title={t('infoTooltips.guidedMode')}>
                  <div>
                    <YBButton
                      className={formMode === ResilienceFormMode.GUIDED ? 'yb-active' : ''}
                      startIcon={
                        formMode === ResilienceFormMode.FREE_FORM ? (
                          <DocTickUnSelected />
                        ) : (
                          <DocTick />
                        )
                      }
                      onClick={() => {
                        methods.setValue(RESILIENCE_FORM_MODE, ResilienceFormMode.GUIDED);
                      }}
                      dataTestId='guided-mode-button'
                    >
                      {t('formType.guidedMode')}
                    </YBButton>
                  </div>
                </YBTooltip>
                <YBTooltip
                  title={<Trans t={t} i18nKey="infoTooltips.freeForm" components={{ b: <b /> }} />}
                >
                  <div>
                    <YBButton
                      className={formMode === ResilienceFormMode.FREE_FORM ? 'yb-active' : ''}
                      onClick={() => {
                        methods.setValue(RESILIENCE_FORM_MODE, ResilienceFormMode.FREE_FORM);
                      }}
                      dataTestId='free-form-mode-button'
                    >
                      {t('formType.freeForm')}
                    </YBButton>
                  </div>
                </YBTooltip>
              </ButtonGroup>
            </Grid>
          </StyledHeader>
          <StyledContent style={{ display: 'flex', gap: '24px', flexDirection: 'column' }}>
            {formMode === ResilienceFormMode.GUIDED ? <GuidedMode /> : <FreeFormMode />}
          </StyledContent>
        </StyledPanel>
      )}

      <div style={{ marginTop: '24px' }}>
        <RegionSelection />
      </div>
      <StyledHelpText>
        <Flash />
        <Trans t={t} i18nKey="helpText" components={{ a: <a /> }} />
      </StyledHelpText>
      {errors?.faultToleranceType?.message && (
        <div style={{ marginTop: '16px' }}>
          <YBAlert
            open
            variant={AlertVariant.Error}
            text={
              <Trans
                t={t}
                i18nKey={errors?.faultToleranceType?.message}
                components={{ b: <b /> }}
                values={{
                  selected_regions: regions.length,
                  required_regions: faultToleranceForRegion,
                  availability_zone: availabilityZoneCount,
                  required_zones: faultToleranceforAz
                }}
              >
                {errors.faultToleranceType.message}
              </Trans>
            }
          />
        </div>
      )}
    </FormProvider>
  );
});

ResilienceAndRegions.displayName = 'ResilienceAndRegions';
