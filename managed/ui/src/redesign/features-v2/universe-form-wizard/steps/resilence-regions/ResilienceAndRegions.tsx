/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { FormProvider, useForm } from 'react-hook-form';
import { ResilienceAndRegionsProps } from './dtos';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { useTranslation } from 'react-i18next';
import { ResilienceTypeField } from '../../fields/resilience-type/ResilienceType';

export const ResilienceAndRegions = forwardRef<StepsRef>((_, forwardRef) => {
  const [, { moveToNextPage, moveToPreviousPage }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions'
  });

  const methods = useForm<ResilienceAndRegionsProps>({});

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        moveToNextPage();
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
      <StyledPanel>
        <StyledHeader>{t('title')}</StyledHeader>
        <StyledContent></StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

ResilienceAndRegions.displayName = 'ResilienceAndRegions';
