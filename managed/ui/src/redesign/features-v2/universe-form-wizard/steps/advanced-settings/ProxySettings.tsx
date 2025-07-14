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
import { useTranslation } from 'react-i18next';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import { ProxyAdvancedProps } from './dtos';
import { EnableProxyServer } from '../../fields';

export const ProxySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [, { moveToNextPage, moveToPreviousPage, saveProxySettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation();

  const methods = useForm<ProxyAdvancedProps>({});

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          saveProxySettings(data);
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
        <StyledHeader>{t('createUniverseV2.proxySettings.header')}</StyledHeader>
        <StyledContent>
          <EnableProxyServer disabled={false} />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

ProxySettings.displayName = 'ProxySettings';
