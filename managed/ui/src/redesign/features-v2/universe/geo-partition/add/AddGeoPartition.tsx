import { FC, useEffect } from 'react';
import { useMethods } from 'react-use';
import { styled } from '@material-ui/core';
import { mui, Step, YBMultiLevelStepper } from '@yugabyte-ui-library/core';
import { useQuery } from 'react-query';

import { useTranslation } from 'react-i18next';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextProps,
  AddGeoPartitionSteps,
  initialAddGeoPartitionFormState,
  GeoPartition,
  addGeoPartitionFormMethods
} from './AddGeoPartitionContext';
import { SwitchGeoPartitionSteps } from './SwitchGeoPartitionSteps';
import {
  computeIsNewGeoPartitionFromUniverse,
  extractRegionsAndNodeDataFromUniverse,
  extractRegionsFromPartitionPlacement,
  getDefaultPrimaryPartitionSpec,
  getExistingGeoPartitions,
  navigateToUniverseSettingsFromWizard,
  useGetSteps
} from './AddGeoPartitionUtils';
import { GeoPartitionInfoModal } from './GeoPartitionInfoModal';
import { getUniverse } from '@app/v2/api/universe/universe';
import { api } from '@app/redesign/helpers/api';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';

import YBLogo from '../../../../assets/yb_logo.svg';
import Close from '../../../../assets/close rounded inverted.svg';

const { Grid2: Grid, Typography } = mui;

const GeoPartitionRoot = styled('div')(() => ({
  '& .full-height-container': {
    backgroundColor: '#fff !important'
  }
}));

interface AddGeoPartitionProps {
  params: {
    uuid?: string;
  };
}

export const AddGeoPartition: FC<AddGeoPartitionProps> = (props) => {
  const { t } = useTranslation('translation', { keyPrefix: 'geoPartition.addGeoPartition' });

  const addGeoPartitionContextData = useMethods(
    addGeoPartitionFormMethods,
    initialAddGeoPartitionFormState
  );

  const [addGeoPartitionContext, addGeoPartitionMethods] = addGeoPartitionContextData;
  const { geoPartitions, activeGeoPartitionIndex, activeStep } = addGeoPartitionContext;

  const universeUUID = props.params?.uuid ?? '';

  const { data: universeData, isSuccess, isLoading: isUniverseDataLoading } = useQuery(
    [universeUUID],
    () => getUniverse(universeUUID),
    {
      onSuccess(data) {
        addGeoPartitionMethods.setUniverseData(data);
        const existingGeoParitionsCount = getExistingGeoPartitions(data).length;
        const defaultPartition = getDefaultPrimaryPartitionSpec(data);
        addGeoPartitionMethods.updateGeoPartition({
          geoPartition: {
            ...geoPartitions[0],
            name:
              defaultPartition?.name ??
              `Geo Partition ${existingGeoParitionsCount + 1}`,
            tablespaceName:
              defaultPartition?.tablespace_name ??
              `Tablespace_${existingGeoParitionsCount + 1}`
          } as GeoPartition,
          activeGeoPartitionIndex: 0
        });
      }
    }
  );
  const provider = universeData?.spec?.clusters[0].provider_spec.provider;

  useQuery([universeUUID, provider], () => api.fetchProviderRegions(provider), {
    enabled: isSuccess && !!provider,
    onSuccess(providerRegionList) {
      addGeoPartitionMethods.setUniverseData(universeData!);
      const defaultPartition = getDefaultPrimaryPartitionSpec(universeData!);
      const regions = defaultPartition
        ? extractRegionsFromPartitionPlacement(defaultPartition, providerRegionList)
        : extractRegionsAndNodeDataFromUniverse(universeData!, providerRegionList).regions;

      addGeoPartitionMethods.updateGeoPartition({
        geoPartition: {
          ...geoPartitions[0],
          resilience: {
            ...geoPartitions[0].resilience,
            regions
          } as any
        },
        activeGeoPartitionIndex: 0
      });
    }
  });

  const steps: Step[] = useGetSteps(addGeoPartitionContext);

  const previousPartitionStepCount = steps.slice(0, activeGeoPartitionIndex).reduce((acc, step) => {
    return acc + step.subSteps.length;
  }, 0);

  useEffect(() => {
    addGeoPartitionMethods.setIsNewGeoPartition(computeIsNewGeoPartitionFromUniverse(universeData));
  }, [universeData]);

  if (isUniverseDataLoading) return <YBLoadingCircleIcon />;

  return (
    <AddGeoPartitionContext.Provider
      value={([...addGeoPartitionContextData, {}] as unknown) as AddGeoPartitionContextProps}
    >
      <GeoPartitionRoot>
        <Grid
          container
          sx={{
            backgroundColor: '#F7F9FB',
            height: '64px',
            padding: '8px 24px',
            justifyContent: 'space-between',
            alignItems: 'center',
            borderBottom: '1px solid #E9EEF2'
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center' }}>
            <YBLogo />
            <Typography
              variant="h4"
              sx={{ color: '#1E154B', fontSize: '18px', fontWeight: 600, marginLeft: '16px' }}
            >
              {t('title')}
            </Typography>
          </div>
          <Close
            style={{ cursor: 'pointer' }}
            onClick={() => navigateToUniverseSettingsFromWizard(universeData)}
          />
        </Grid>
        <Grid container spacing={2}>
          <Grid sx={{ borderRight: '1px solid #E9EEF2', height: '100vh' }}>
            <YBMultiLevelStepper
              dataTestId="stepper"
              activeStep={
                previousPartitionStepCount +
                (activeStep === AddGeoPartitionSteps.REVIEW ? 1 : activeStep)
              }
              steps={steps}
            />
          </Grid>
          <Grid
            container
            direction="column"
            size="grow"
            sx={{ padding: '16px', maxWidth: '1024px', minWidth: '856px', gap: 0 }}
          >
            <SwitchGeoPartitionSteps />
          </Grid>
        </Grid>
      </GeoPartitionRoot>
      <GeoPartitionInfoModal />
    </AddGeoPartitionContext.Provider>
  );
};
