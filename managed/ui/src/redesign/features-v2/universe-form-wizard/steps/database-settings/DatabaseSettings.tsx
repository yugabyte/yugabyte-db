import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { FormProvider, useForm } from 'react-hook-form';
// import { yupResolver } from '@hookform/resolvers/yup';
// import { useTranslation } from 'react-i18next';
import { styled, Typography } from '@material-ui/core';
import { DatabaseSettingsProps } from './dtos';
import { YCQLFIELD, YSQLField, ConnectionPoolingField, PGCompatibiltyField } from '../../fields';
import { mui } from '@yugabyte-ui-library/core';

const { Box } = mui;

const StyledPanel = styled('div')(({ theme }) => ({
  padding: '0',
  backgroundColor: '#fff',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  width: '100%'
}));

const StyledHeader = styled(Typography)(({ theme }) => ({
  padding: `10px ${theme.spacing(3)}px`,
  fontSize: 15,
  color: theme.palette.grey[900]
}));

const StyledContent = styled('div')(({ theme }) => ({
  padding: `${theme.spacing(1)}px ${theme.spacing(2.5)}px ${theme.spacing(2.5)}px ${theme.spacing(
    2.5
  )}px`,
  display: 'flex',
  gap: theme.spacing(3),
  flexDirection: 'column'
}));

export const DatabaseSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [, { moveToNextPage }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const methods = useForm<DatabaseSettingsProps>();

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          console.log(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {}
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
      </StyledPanel>
    </FormProvider>
  );
});
