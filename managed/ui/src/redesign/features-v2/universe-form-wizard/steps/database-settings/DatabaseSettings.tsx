import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { FormProvider, useForm } from 'react-hook-form';
// import { yupResolver } from '@hookform/resolvers/yup';
// import { useTranslation } from 'react-i18next';
import { DatabaseSettingsProps } from './dtos';
import { YCQLFIELD, YSQLField, ConnectionPoolingField, PGCompatibiltyField } from '../../fields';
import { mui } from '@yugabyte-ui-library/core';
import { StyledPanel, StyledHeader } from '../../components/DefaultComponents';

const { Box, styled, Typography } = mui;

const StyledContent = styled('div')(({ theme }) => ({
  padding: `${theme.spacing(1)}px ${theme.spacing(2.5)}px ${theme.spacing(2.5)}px ${theme.spacing(
    2.5
  )}px`,
  display: 'flex',
  gap: theme.spacing(3),
  flexDirection: 'column'
}));

export const DatabaseSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [, { moveToNextPage, moveToPreviousPage }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const methods = useForm<DatabaseSettingsProps>();

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
      <StyledPanel>
        <StyledHeader>{'Interface'}</StyledHeader>
        <StyledContent>
          <YSQLField />
          <YCQLFIELD />
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{'Features'}</StyledHeader>
        <StyledContent>
          <ConnectionPoolingField disabled={false} />
          <PGCompatibiltyField disabled={false} />
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{'Advanced Flags'}</StyledHeader>
        <StyledContent>Work in progress</StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});
