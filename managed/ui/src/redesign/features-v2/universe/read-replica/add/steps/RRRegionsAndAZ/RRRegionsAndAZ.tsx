import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation, Trans } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import { StepsRef, AddRRContext, AddRRContextMethods } from '../../AddReadReplicaContext';
import InfoIcon from '@app/redesign/assets/book_open_blue.svg';

const { Box, styled } = mui;

const StyledBanner = styled(Box)(({ theme }) => ({
  padding: `${theme.spacing(2)} ${theme.spacing(3)}`,
  display: 'flex',
  alignItems: 'center',
  gap: theme.spacing(2),
  borderRadius: '8px',
  border: `1px solid #CBCCFB`,
  color: theme.palette.grey[700],
  '& a': {
    color: theme.palette.primary[600],
    textDecoration: 'underline'
  }
}));

export const RRRegionsAndAZ = forwardRef<StepsRef>((_, forwardRef) => {
  const [{}, { moveToNextPage }] = (useContext(AddRRContext) as unknown) as AddRRContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });

  const methods = useForm<any>({
    // resolver: yupResolver(GeneralSettingsValidationSchema(t)),
    defaultValues: {},
    mode: 'onChange'
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          moveToNextPage();
        })();
      },
      onPrev: () => {}
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <RRBreadCrumbs groupTitle={t('placement')} subTitle={t('regionsAndAZ')} />
        <StyledBanner>
          <InfoIcon />
          <div>
            <Trans t={t} i18nKey="regionAZHelpText" components={{ a: <a href="#" /> }} />
          </div>
        </StyledBanner>
      </Box>
    </FormProvider>
  );
});
