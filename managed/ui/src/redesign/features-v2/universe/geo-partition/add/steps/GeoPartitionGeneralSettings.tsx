import { useContext } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import * as Yup from 'yup';
import { mui, yba, YBInput, YBInputField, YBTag } from '@yugabyte-ui-library/core';
import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionSteps,
  GeoPartition,
  initialAddGeoPartitionFormState
} from '../AddGeoPartitionContext';
import { getExistingGeoPartitions, useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import {
  StyledContent,
  StyledHeader,
  StyledPanel
} from '../../../create-universe/components/DefaultComponents';
import { getFlagFromRegion } from '../../../create-universe/helpers/RegionToFlagUtils';

import { yupResolver } from '@hookform/resolvers/yup';
import InfoIcon from '@app/redesign/assets/book_open_blue.svg';

const { Box, styled, Typography, typographyClasses } = mui;

const StyledDefaultRegionsInGeoPartition = styled('div')(({ theme }) => ({
  padding: `16px 24px`,
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  backgroundColor: '#F7F9FB',
  display: 'flex',
  gap: '10px',
  flexDirection: 'column',
  [`& >.${typographyClasses.root}`]: {
    color: theme.palette.grey[700]
  }
}));

const StyledRegionContainer = styled('div')((theme) => ({
  width: 'fit-content',
  '&>div': {
    marginLeft: 0
  }
}));

const StyledGeoPartitionHelpBanner = styled(Box)(({ theme }) => ({
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

export const GeoPartitionGeneralSettings = () => {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;

  const { updateGeoPartition, addGeoPartition } = addGeoPartitionMethods;
  const {
    activeGeoPartitionIndex,
    isNewGeoPartition,
    geoPartitions,
    activeStep
  } = addGeoPartitionContext;
  const currentGeoPartition = addGeoPartitionContext.geoPartitions[activeGeoPartitionIndex];

  const { moveToNextPage, moveToPreviousPage } = useGeoPartitionNavigation();
  const { t } = useTranslation('translation', {
    keyPrefix: 'geoPartition.geoPartitionGeneralSettings'
  });
  const form = useForm<GeoPartition>({
    defaultValues: {
      name: currentGeoPartition.name,
      tablespaceName: currentGeoPartition.tablespaceName
    },
    resolver: yupResolver(
      Yup.object({
        tablespaceName: Yup.string().required(t('tablespaceNameRequiredError')),
        name: Yup.string().required(t('displayNameRequiredError'))
      })
    )
  });
  const { control, handleSubmit, setValue } = form;

  const alreadyExistingGeoParitionsCount = getExistingGeoPartitions(
    addGeoPartitionContext.universeData!
  ).length;

  const addNewGeoPartition = () => {
    addGeoPartition({
      ...initialAddGeoPartitionFormState.geoPartitions[0],
      name: `Geo Partition ${alreadyExistingGeoParitionsCount + geoPartitions.length + 1}`,
      tablespaceName: 'Tablespace 1'
    });
  };

  return (
    <FormProvider {...form}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <GeoPartitionBreadCrumb
          groupTitle={
            <>
              {currentGeoPartition.name}
              {isNewGeoPartition && activeGeoPartitionIndex === 0 && (
                <span style={{ marginLeft: '12px' }}>
                  <YBTag color="purple" size="medium">
                    Primary
                  </YBTag>
                </span>
              )}
            </>
          }
          subTitle={<>{t('title')}</>}
        />
        {isNewGeoPartition && activeGeoPartitionIndex === 0 && (
          <StyledGeoPartitionHelpBanner>
            <InfoIcon />
            <div>
              <Trans t={t} i18nKey="helpText" components={{ a: <a href="#" /> }} />
            </div>
          </StyledGeoPartitionHelpBanner>
        )}
        <StyledPanel>
          <StyledHeader>{t('title')}</StyledHeader>
          <StyledContent>
            <YBInputField
              name="name"
              label={t('displayName')}
              control={control}
              dataTestId="geo-partition-name-input"
              value={currentGeoPartition.name}
              onChange={(e) => {
                updateGeoPartition({
                  geoPartition: {
                    ...currentGeoPartition,
                    name: e.target.value
                  },
                  activeGeoPartitionIndex
                });
                setValue('name', e.target.value);
              }}
            />
            <YBInputField
              label={t('tablespaceName')}
              dataTestId="geo-partition-tablespace-input"
              value={currentGeoPartition.tablespaceName}
              control={control}
              name="tablespaceName"
              onChange={(e) => {
                updateGeoPartition({
                  geoPartition: {
                    ...currentGeoPartition,
                    tablespaceName: e.target.value
                  },
                  activeGeoPartitionIndex
                });
                setValue('tablespaceName', e.target.value);
              }}
              helperText={
                <Trans t={t} i18nKey="tablespaceNameHelpText" components={{ b: <b /> }} />
              }
            />
            {isNewGeoPartition && activeGeoPartitionIndex === 0 && (
              <StyledDefaultRegionsInGeoPartition>
                <Typography variant="body2" color="textDisabled">
                  {t('existingRegions')}
                </Typography>
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '8px' }}
                >
                  {currentGeoPartition?.resilience?.regions.map((region) => (
                    <StyledRegionContainer key={region.uuid}>
                      <YBTag key={region.uuid} size="medium" disabled variant="light">
                        {getFlagFromRegion(region.code)} {region.name} ({region.code})
                      </YBTag>
                    </StyledRegionContainer>
                  ))}
                </Box>
              </StyledDefaultRegionsInGeoPartition>
            )}
          </StyledContent>
        </StyledPanel>
        <UniverseActionButtons
          prevButton={{
            text: t('back', { keyPrefix: 'common' }),
            onClick: moveToPreviousPage,
            disabled:
              activeGeoPartitionIndex === 0 && activeStep === AddGeoPartitionSteps.GENERAL_SETTINGS
          }}
          cancelButton={{
            text: t('cancel', { keyPrefix: 'common' }),
            onClick: () => {}
          }}
          nextButton={{
            text:
              activeGeoPartitionIndex === 0 && isNewGeoPartition
                ? t('addNewGeoPartition')
                : t('next', { keyPrefix: 'common' }),
            onClick: () => {
              handleSubmit(() => {
                if (activeGeoPartitionIndex === 0 && isNewGeoPartition) {
                  addNewGeoPartition();
                } else {
                  moveToNextPage(addGeoPartitionContext);
                }
              })();
            }
          }}
        />
      </Box>
    </FormProvider>
  );
};
