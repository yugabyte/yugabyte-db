/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { mui } from '@yugabyte-ui-library/core';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { InstanceSettingProps } from './dtos';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { CPUArchField } from '../../fields';
// import { useTranslation } from 'react-i18next';

const { Box } = mui;

export const InstanceSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { instanceSettings },
    { moveToNextPage, moveToPreviousPage, saveInstanceSettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  //   const { t } = useTranslation('translation', {
  //     keyPrefix: 'createUniverseV2.resilienceAndRegions'
  //   });

  const methods = useForm<InstanceSettingProps>({
    defaultValues: instanceSettings
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          saveInstanceSettings(data);
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
      <StyledPanel>
        <StyledHeader>Cluster Instance</StyledHeader>
        <StyledContent>
          <Box
            sx={{
              display: 'flex',
              width: '734px',
              flexDirection: 'column',
              backgroundColor: '#FBFCFD',
              border: '1px solid #D7DEE4',
              borderRadius: '8px',
              padding: '24px'
            }}
          >
            <CPUArchField disabled={false} />
            <br />
            <br />
            <>Remaining fields Work In progress</>
          </Box>
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

InstanceSettings.displayName = 'InstanceSettings';
