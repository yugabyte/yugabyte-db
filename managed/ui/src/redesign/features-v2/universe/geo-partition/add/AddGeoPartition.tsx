import { FC } from 'react';
import { styled } from '@material-ui/core';
import { mui, Step, YBMultiLevelStepper } from '@yugabyte-ui-library/core';

import { ReactComponent as YBLogo } from '../../../../assets/yb_logo.svg';
import { ReactComponent as Close } from '../../../../assets/close rounded inverted.svg';
import { useTranslation } from 'react-i18next';
import { useMethods, useMount } from 'react-use';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextProps,
  addGeoPartitionFormMethods,
  AddGeoPartitionSteps,
  initialAddGeoPartitionFormState
} from './AddGeoPartitionContext';
import { SwitchGeoPartitionSteps } from './SwitchGeoPartitionSteps';
import { useGetSteps } from './AddGeoPartitionUtils';
import { GeoPartitionInfoModal } from './GeoPartitionInfoModal';

const { Grid2: Grid, Typography } = mui;

const GeoPartitionRoot = styled('div')(() => ({
  '& .full-height-container': {
    backgroundColor: '#fff !important'
  }
}));

interface AddGeoPartitionProps {
  isNewGeoPartition?: boolean;
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
  const { activeStep, activeGeoPartitionIndex } = addGeoPartitionContext;
  const { isNewGeoPartition } = props;

  const universeUUID = props.params?.uuid ?? '';

  // const { data } = useQuery([universeUUID], () => getUniverse(universeUUID));
  // console.log(data);

  const steps: Step[] = useGetSteps(addGeoPartitionContext);

  const previousPartitionStepCount = steps.slice(0, activeGeoPartitionIndex).reduce((acc, step) => {
    return acc + step.subSteps.length;
  }, 0);

  useMount(() => {
    addGeoPartitionMethods.setIsNewGeoPartition(isNewGeoPartition);
  });

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
          <Close style={{ cursor: 'pointer' }} />
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
