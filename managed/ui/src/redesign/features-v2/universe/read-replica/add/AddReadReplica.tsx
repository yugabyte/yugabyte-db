import { FC, useMemo, useRef } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { mui, YBMultiLevelStepper, yba } from '@yugabyte-ui-library/core';
import { useMethods } from 'react-use';
import { styled } from '@material-ui/core';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import {
  AddRRContext,
  AddRRContextProps,
  addRRFormMethods,
  initialAddRRFormState,
  StepsRef
} from './AddReadReplicaContext';
import AuthenticatedArea from '@app/pages/AuthenticatedArea';
import { getUniverse } from '@app/v2/api/universe/universe';

import YBLogo from '../../../../assets/yb_logo.svg';
import Close from '../../../../assets/close rounded inverted.svg';
import { SwitchRRSteps } from './SwitchRRSteps';
import { getRRSteps, getInitialValues } from './AddReadReplicaUtils';

const { Grid2: Grid, Typography } = mui;
const { YBButton } = yba;

const ReadReplicaRoot = styled('div')(() => ({
  '& .full-height-container': {
    backgroundColor: '#fff !important'
  }
}));

interface AddRRProps {
  params: {
    uuid?: string;
  };
}

export const AddReadReplica: FC<AddRRProps> = (props) => {
  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });

  const addRRContextData = useMethods(addRRFormMethods, initialAddRRFormState);

  const [addRRContext, addRRMethods] = addRRContextData;
  const { activeStep } = addRRContext;
  const steps = useMemo(() => getRRSteps(t), [t]);
  const currentStepRef = useRef<StepsRef>(null);

  const universeUUID = props.params?.uuid ?? '';

  const { data: universeData, isSuccess, isLoading: isUniverseDataLoading } = useQuery(
    [universeUUID],
    () => getUniverse(universeUUID),
    {
      onSuccess(data) {
        addRRMethods.setUniverseData(data);
        addRRMethods.initializeDefaultValues(getInitialValues(data));
      }
    }
  );

  //fetch provider Regions for first step

  if (isUniverseDataLoading) return <YBLoadingCircleIcon />;

  return (
    <ReadReplicaRoot>
      <AuthenticatedArea simpleMode>
        <AddRRContext.Provider value={([...addRRContextData, {}] as unknown) as AddRRContextProps}>
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
              <YBMultiLevelStepper dataTestId="stepper" activeStep={activeStep} steps={steps} />
            </Grid>
            <Grid
              container
              direction="column"
              size="grow"
              sx={{ padding: '16px', maxWidth: '1024px', minWidth: '856px', gap: 0 }}
            >
              <SwitchRRSteps ref={currentStepRef} />
              <Grid
                container
                alignItems="center"
                justifyContent="space-between"
                direction="row"
                sx={{ marginTop: '32px' }}
              >
                <YBButton variant="secondary" size="large" dataTestId="add-rr-cancel-button">
                  {t('cancel', { keyPrefix: 'common' })}
                </YBButton>
                <Grid container alignItems="center" justifyContent="flex-end" spacing={2}>
                  <YBButton
                    onClick={() => {
                      currentStepRef.current?.onPrev();
                    }}
                    disabled={activeStep === 1}
                    variant="secondary"
                    size="large"
                    dataTestId="add-rr-back-button"
                  >
                    {t('back', { keyPrefix: 'common' })}
                  </YBButton>
                  <YBButton
                    onClick={() => {
                      currentStepRef.current?.onNext();
                    }}
                    variant="ybaPrimary"
                    size="large"
                    dataTestId="add-rr-next-button"
                  >
                    {t(activeStep === 4 ? 'create' : 'next', { keyPrefix: 'common' })}
                  </YBButton>
                </Grid>
              </Grid>
            </Grid>
          </Grid>
        </AddRRContext.Provider>
      </AuthenticatedArea>
    </ReadReplicaRoot>
  );
};
