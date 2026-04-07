/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle, useRef, useState } from 'react';
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
import { GuidedMode, ExpertMode, RegionSelection } from './index';
import { ResilienceAndRegionsSchema } from './ValidationSchema';
import {
  computeResilienceTypeFromProvider,
  getFaultToleranceNeeded
} from '../../CreateUniverseUtils';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  initialCreateUniverseFormState,
  StepsRef
} from '../../CreateUniverseContext';
import { FaultToleranceType, ResilienceAndRegionsProps, ResilienceFormMode, ResilienceType } from './dtos';
import {
  FAULT_TOLERANCE_TYPE,
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../../fields/FieldNames';

//icons
import MapIcon from '@app/redesign/assets/map.svg';
import MapIconSelected from '@app/redesign/assets/map_selected.svg';
import Flash from '@app/redesign/assets/flash_transparent.svg';

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
    resolver: yupResolver(ResilienceAndRegionsSchema(t))
  });

  const { watch, trigger, clearErrors } = methods;

  const formMode = watch(RESILIENCE_FORM_MODE);
  const regions = watch(REGIONS_FIELD);
  const resilienceFactor = watch(RESILIENCE_FACTOR);
  const faultToleranceType = watch(FAULT_TOLERANCE_TYPE);
  const faultToleranceForRegion = getFaultToleranceNeeded(resilienceFactor);
  const faultToleranceforAz = getFaultToleranceNeeded(resilienceFactor);
  const resilienceType = watch(RESILIENCE_TYPE);

  const availabilityZoneCount = regions.reduce((acc, region) => {
    return acc + region.zones?.length;
  }, 0);

  const { errors, isSubmitted } = methods.formState;
  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);

  useEffect(() => {
    setResilienceType(resilienceType);

    //reset nodes availability settings when resilience type changes
    saveNodesAvailabilitySettings(initialCreateUniverseFormState.nodesAvailabilitySettings!);
  }, [resilienceType]);

  const prevFormModeRef = useRef<ResilienceFormMode | null>(null);

  useEffect(() => {
    if (prevFormModeRef.current === null) {
      prevFormModeRef.current = formMode;
      return;
    }
    if (prevFormModeRef.current === formMode) {
      return;
    }
    prevFormModeRef.current = formMode;

    saveNodesAvailabilitySettings(initialCreateUniverseFormState.nodesAvailabilitySettings!);

    if (formMode === ResilienceFormMode.EXPERT_MODE) {
      const ft = methods.getValues(FAULT_TOLERANCE_TYPE);
      if (ft === FaultToleranceType.NODE_LEVEL || ft === FaultToleranceType.NONE) {
        methods.setValue(FAULT_TOLERANCE_TYPE, FaultToleranceType.AZ_LEVEL, { shouldValidate: true });
        saveResilienceAndRegionsSettings({
          ...methods.getValues(),
          [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL
        });
      }
    }
  }, [formMode, methods, saveNodesAvailabilitySettings, saveResilienceAndRegionsSettings]);

  // When guided/expert mode, fault tolerance type, RF, or resilience type changes, hide submit
  // errors until the user clicks Next again (do not carry forward stale validation UI).
  const resilienceGoalRef = useRef<{
    formMode: ResilienceFormMode;
    faultToleranceType: string;
    resilienceFactor: number;
    resilienceType: ResilienceType;
  } | null>(null);

  useEffect(() => {
    const next = {
      formMode,
      faultToleranceType,
      resilienceFactor,
      resilienceType
    };
    if (resilienceGoalRef.current === null) {
      resilienceGoalRef.current = next;
      return;
    }
    const prev = resilienceGoalRef.current;
    const goalChanged =
      prev.formMode !== next.formMode ||
      prev.faultToleranceType !== next.faultToleranceType ||
      prev.resilienceFactor !== next.resilienceFactor ||
      prev.resilienceType !== next.resilienceType;
    if (goalChanged) {
      setShowErrorsAfterSubmit(false);
      clearErrors();
      resilienceGoalRef.current = next;
    }
  }, [formMode, faultToleranceType, resilienceFactor, resilienceType, clearErrors]);

  // Re-validate when regions change after submit; when form becomes valid, hide errors without another Next.
  useEffect(() => {
    if (isSubmitted) {
      trigger().then((isValid) => {
        if (isValid) setShowErrorsAfterSubmit(false);
      });
    }
  }, [regions, isSubmitted, trigger]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        setShowErrorsAfterSubmit(true);
        return methods.handleSubmit((data) => {
          saveResilienceAndRegionsSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      },
      setValue: methods.setValue as (name: string, value: unknown) => void
    }),
    [methods]
  );

  useMount(() => {
    if (regions.length !== 0) return;

    if (!isGeoPartition && generalSettings?.providerConfiguration) {
      const computedFaultToleranceType = computeResilienceTypeFromProvider(
        generalSettings.providerConfiguration
      );
      methods.setValue(FAULT_TOLERANCE_TYPE, computedFaultToleranceType[FAULT_TOLERANCE_TYPE]);
      methods.setValue(RESILIENCE_FACTOR, computedFaultToleranceType[RESILIENCE_FACTOR]);
    }
  });

  useEffect(() => {
    // if the user switches to guided mode and has set a high replication factor, reduce it to 3 which guided mode supports
    if (formMode === ResilienceFormMode.GUIDED && resilienceFactor > 3) {
      methods.setValue(RESILIENCE_FACTOR, 3, { shouldValidate: true });
    }

    // in free form mode, replication factor should always be odd
    if (formMode === ResilienceFormMode.EXPERT_MODE && resilienceFactor % 2 === 0) {
      methods.setValue(RESILIENCE_FACTOR, resilienceFactor + 1, { shouldValidate: true });
    }
  }, [formMode]);

  useEffect(() => {
    if (faultToleranceType === FaultToleranceType.NONE && resilienceFactor > 1) {
      methods.setValue(RESILIENCE_FACTOR, 1, { shouldValidate: true });
    }
  }, [faultToleranceType, resilienceFactor]);

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
        <>
          <Grid alignItems={'center'} justifyContent={'flex-end'} container width="100%">
            <YBButtonGroup
              size="large"
              dataTestId="yb-button-group-multiselect-normal"
              value={formMode}
              buttons={[
                {
                  value: ResilienceFormMode.GUIDED,
                  label: t('formType.guidedMode'),
                  icon: formMode === ResilienceFormMode.GUIDED ? <MapIconSelected /> : <MapIcon />,
                  onClick: () => {
                    methods.setValue(RESILIENCE_FORM_MODE, ResilienceFormMode.GUIDED, {
                      shouldValidate: true
                    });
                  },
                  buttonProps: {
                    dataTestId: 'guided-mode-button'
                  }
                },
                {
                  value: ResilienceFormMode.EXPERT_MODE,
                  label: t('formType.expertMode'),
                  onClick: () => {
                    methods.setValue(RESILIENCE_FORM_MODE, ResilienceFormMode.EXPERT_MODE, {
                      shouldValidate: true
                    });
                  },
                  buttonProps: {
                    dataTestId: 'expert-mode-button'
                  }
                }
              ]}
            />
          </Grid>
          {formMode === ResilienceFormMode.GUIDED ? <GuidedMode /> : <ExpertMode />}
        </>
      )}
      <RegionSelection showErrorsAfterSubmit={showErrorsAfterSubmit} />
      {!hideHelpText && (
        <StyledHelpText>
          <Flash />
          <Trans t={t} i18nKey="helpText" components={{ a: <a /> }} />
        </StyledHelpText>
      )}
      {showErrorsAfterSubmit && errors?.faultToleranceType?.message && (
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
