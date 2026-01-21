/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { useMount } from 'react-use';
import { Trans, useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import {
  AlertVariant,
  mui,
  YBAlert,
  YBButtonGroup,
  YBSmartStatus,
  StatusType,
  IconPosition
} from '@yugabyte-ui-library/core';
import { ResilienceTypeField } from '../../fields';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { GuidedMode, FreeFormMode, RegionSelection } from './index';
import { ResilienceAndRegionsSchema } from './ValidationSchema';
import {
  computeFaultToleranceTypeFromProvider,
  getFaultToleranceNeeded,
  getFaultToleranceNeededForAZ
} from '../../CreateUniverseUtils';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  initialCreateUniverseFormState,
  StepsRef
} from '../../CreateUniverseContext';
import { ResilienceAndRegionsProps, ResilienceFormMode, ResilienceType } from './dtos';
import {
  FAULT_TOLERANCE_TYPE,
  REGIONS_FIELD,
  REPLICATION_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../fields/FieldNames';

//icons
import DocTick from '../../../../../assets/doc_tick.svg';
import DocTickUnSelected from '../../../../../assets/doc_tick_unselected.svg';
import Flash from '../../../../../assets/flash_transparent.svg';

const { Grid2: Grid, Collapse, styled, Box } = mui;

const StyledHelpText = styled('div')(({ theme }) => ({
  padding: '16px 24px',
  display: 'flex',
  gap: '8px',
  color: theme.palette.grey[700],
  fontSize: '13px',
  fontWeight: 400,
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  alignItems: 'center',
  '& > a': {
    color: theme.palette.grey[700],
    textDecoration: 'underline',
    cursor: 'pointer'
  }
}));

export const ResilienceAndRegions = forwardRef<
  StepsRef,
  { isGeoPartition?: boolean; hideHelpText?: boolean }
>(({ isGeoPartition = false, hideHelpText = false }, forwardRef) => {
  const [
    { generalSettings, resilienceAndRegionsSettings },
    {
      moveToPreviousPage,
      saveResilienceAndRegionsSettings,
      saveNodesAvailabilitySettings,
      moveToNextPage,
      setResilienceType
    }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions'
  });

  const methods = useForm<ResilienceAndRegionsProps>({
    defaultValues: resilienceAndRegionsSettings,
    resolver: yupResolver(ResilienceAndRegionsSchema(t)),
    mode: 'onSubmit'
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
    return acc + region.zones?.length;
  }, 0);

  const { errors, isSubmitted } = methods.formState;

  useEffect(() => {
    trigger(FAULT_TOLERANCE_TYPE);
  }, [regions, replicationFactor, faultToleranceType, formMode, resilienceType]);

  useEffect(() => {
    setResilienceType(resilienceType);

    //reset nodes availability settings when resilience type changes
    saveNodesAvailabilitySettings(initialCreateUniverseFormState.nodesAvailabilitySettings!);
  }, [resilienceType]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
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

  useMount(() => {
    if (regions.length !== 0) return;

    if (!isGeoPartition && generalSettings?.providerConfiguration) {
      const computedFaultToleranceType = computeFaultToleranceTypeFromProvider(
        generalSettings.providerConfiguration
      );
      methods.setValue(FAULT_TOLERANCE_TYPE, computedFaultToleranceType[FAULT_TOLERANCE_TYPE]);
      methods.setValue(REPLICATION_FACTOR, computedFaultToleranceType[REPLICATION_FACTOR]);
    }
  });

  return (
    <FormProvider {...methods}>
      {!isGeoPartition && <ResilienceTypeField<ResilienceAndRegionsProps> name="resilienceType" />}
      {resilienceType === ResilienceType.SINGLE_NODE && (
        <Collapse in={resilienceType === ResilienceType.SINGLE_NODE}>
          <Box
            sx={{
              display: 'flex',
              flexDirection: 'row',
              gap: '8px',
              alignItems: 'center',
              color: '#4E5F6D',
              marginTop: '-8px'
            }}
          >
            <YBSmartStatus
              type={StatusType.WARNING}
              label={t('singleNode.caution')}
              iconPosition={IconPosition.NONE}
            />
            {t('singleNode.cautionMsg')}
          </Box>
        </Collapse>
      )}
      {resilienceType === ResilienceType.REGULAR && (
        <StyledPanel>
          <StyledHeader>
            <Grid alignItems={'center'} justifyContent={'space-between'} container width="100%">
              {t('title')}
              {/* TODO: Missing Tooltip, needs to be added as a prop in CCL */}
              <YBButtonGroup
                size="large"
                dataTestId="yb-button-group-multiselect-normal"
                value={ResilienceFormMode.GUIDED}
                buttons={[
                  {
                    value: ResilienceFormMode.GUIDED,
                    label: t('formType.guidedMode'),
                    icon:
                      formMode === ResilienceFormMode.FREE_FORM ? (
                        <DocTickUnSelected />
                      ) : (
                        <DocTick />
                      ),
                    onClick: () => {
                      methods.setValue(RESILIENCE_FORM_MODE, ResilienceFormMode.GUIDED);
                    },
                    buttonProps: {
                      dataTestId: 'guided-mode-button'
                    },
                    tooltip: t('infoTooltips.guidedMode')
                  },
                  {
                    value: ResilienceFormMode.FREE_FORM,
                    label: t('formType.freeForm'),
                    onClick: () => {
                      methods.setValue(RESILIENCE_FORM_MODE, ResilienceFormMode.FREE_FORM);
                    },
                    buttonProps: {
                      dataTestId: 'free-form-mode-button'
                    },
                    tooltip: (
                      <Trans t={t} i18nKey="infoTooltips.freeForm" components={{ b: <b /> }} />
                    )
                  }
                ]}
              />
            </Grid>
          </StyledHeader>
          <StyledContent style={{ display: 'flex', gap: '24px', flexDirection: 'column' }}>
            {formMode === ResilienceFormMode.GUIDED ? <GuidedMode /> : <FreeFormMode />}
          </StyledContent>
        </StyledPanel>
      )}
      <RegionSelection />
      {!hideHelpText && (
        <StyledHelpText>
          <Flash />
          <Trans t={t} i18nKey="helpText" components={{ a: <a /> }} />
        </StyledHelpText>
      )}
      {errors?.faultToleranceType?.message && isSubmitted && (
        <div>
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
