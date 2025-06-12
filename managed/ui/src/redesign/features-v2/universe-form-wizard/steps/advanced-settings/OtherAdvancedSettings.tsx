import React from 'react';

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
import { mui, YBAccordion } from '@yugabyte-ui-library/core';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import {
  DeploymentPortsField,
  UserTagsField,
  TimeSyncField,
  InstanceARNField,
  SystemDField,
  AccessKeyField
} from '../../fields';
import { OtherAdvancedProps } from './dtos';

// import { useTranslation } from 'react-i18next';

const { Box } = mui;

export const OtherAdvancedSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [, { moveToNextPage, moveToPreviousPage }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const methods = useForm<OtherAdvancedProps>({
    defaultValues: {
      instanceTags: [
        {
          name: '',
          value: ''
        }
      ]
    }
  });

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
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '24px', mb: 3 }}>
        <YBAccordion titleContent="Deployment Port Override" sx={{ width: '100%' }}>
          <DeploymentPortsField disabled={false} />
        </YBAccordion>
        <YBAccordion titleContent="User Tags" sx={{ width: '100%' }}>
          <UserTagsField disabled={false} />
        </YBAccordion>
      </Box>
      <StyledPanel>
        <StyledHeader>Other Additional Settings</StyledHeader>
        <StyledContent>
          <TimeSyncField disabled={false} />
          <AccessKeyField disabled={false} />
          <InstanceARNField disabled={false} />
          <SystemDField disabled={false} />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});
