import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { FormProvider, useForm } from 'react-hook-form';
// import { yupResolver } from '@hookform/resolvers/yup';
// import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { AssignPublicIPField, EARField, EITField } from '../../fields';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import { SecuritySettingsProps } from './dtos';

const { Box } = mui;

export const SecuritySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [, { moveToNextPage, moveToPreviousPage }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const methods = useForm<SecuritySettingsProps>({});

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
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
        <StyledHeader>{'Public IP assignment'}</StyledHeader>
        <StyledContent>
          <AssignPublicIPField disabled={false} />
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{'Encryption in Transit Settings'}</StyledHeader>
        <StyledContent>
          <EITField disabled={false} />
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{'Encryption at Rest Settings'}</StyledHeader>
        <StyledContent>
          <EARField disabled={false} />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

SecuritySettings.displayName = 'SecuritySettings';
