import { mui, yba, YBInput, YBInputField, YBTagv2 } from '@yugabyte-ui-library/core';
import { UniverseActionButtons } from '../../../create-universe/components/UniverseActionButtons';
import GeoPartitionBreadCrumb from '../GeoPartitionBreadCrumbs';
import { useContext } from 'react';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionSteps,
  GeoPartition,
  initialAddGeoPartitionFormState
} from '../AddGeoPartitionContext';
import { useGeoPartitionNavigation } from '../AddGeoPartitionUtils';
import { Trans, useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { StyledContent, StyledHeader, StyledPanel } from '../../../create-universe/components/DefaultComponents';

const { Box } = mui;
const { YBButton } = yba;

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
  const { t } = useTranslation("translation", { keyPrefix: "geoPartition.geoPartitionGeneralSettings" });
  const form = useForm<GeoPartition>({
    defaultValues: {
      name: currentGeoPartition.name,
      tablespaceName: currentGeoPartition.tablespaceName
    }
  });
  const { control } = form;
  return (
    <FormProvider {...form}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <GeoPartitionBreadCrumb
          groupTitle={
            <>
              {currentGeoPartition.name}
              {isNewGeoPartition && activeGeoPartitionIndex === 0 && (
                <YBTagv2
                  sx={{ marginLeft: '12px' }}
                  text={'Primary'}
                  variant="primary"
                  filled
                  noGradient
                />
              )}
            </>
          }
          subTitle={<>{t('title')}</>}
        />
        <StyledPanel>
          <StyledHeader>{t('title')}</StyledHeader>
          <StyledContent>
            <YBInput
              label={t('displayName')}
              dataTestId="geo-partition-name-input"
              value={currentGeoPartition.name}
              onChange={(e) => {
                updateGeoPartition({
                  geoPartition: {
                    ...currentGeoPartition,
                    name: e.target.value,
                  },
                  activeGeoPartitionIndex
                });
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
              }}
              helperText={<Trans t={t} i18nKey="tablespaceNameHelpText" components={{ b: <b /> }} />}
            />
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
            onClick: () => { }
          }}
          nextButton={{
            text: t('next', { keyPrefix: 'common' }),
            onClick: moveToNextPage
          }}
          additionalButtons={
            isNewGeoPartition && activeGeoPartitionIndex === 0 ? (
              <YBButton
                size={'large'}
                dataTestId="add-geo-partition"
                variant="ybaPrimary"
                color="primary"
                onClick={() => {
                  addGeoPartition({
                    ...initialAddGeoPartitionFormState.geoPartitions[0],
                    name: `Geo Partition ${geoPartitions.length + 1}`,
                    tablespaceName: 'Tablespace 1'
                  });
                }}
              >
                {t('addGeoPartition')}
              </YBButton>
            ) : undefined
          }
        />
      </Box>
    </FormProvider>
  );
};
