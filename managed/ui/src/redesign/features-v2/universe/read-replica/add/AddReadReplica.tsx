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

import { SwitchRRSteps } from './SwitchRRSteps';
import { getRRSteps, getInitialValues } from './AddReadReplicaUtils';
import { getReadReplicaExitRoute } from '../readReplicaUtils';
import YBLogo from '../../../../assets/yb_logo.svg';
import Close from '../../../../assets/close rounded inverted.svg';

const { Grid2: Grid, Typography, Box } = mui;
const { YBButton } = yba;

const ReadReplicaRoot = styled('div')(() => ({
  '& .full-height-container': {
    backgroundColor: '#fff !important',
    display: 'flex',
    height: '100vh',
    width: '100%',
    flexDirection: 'column',
    position: 'relative',
    overflow: 'hidden'
  }
}));

const RRHeader = styled('div')(() => ({
  display: 'flex',
  flexDirection: 'row',
  padding: '8px 24px 8px 20px',
  height: '64px',
  backgroundColor: '#F7F9FB',
  alignItems: 'center',
  justifyContent: 'space-between',
  minWidth: '1200px',
  borderBottom: '1px solid #E9EEF2'
}));

interface AddRRProps {
  params: {
    uuid?: string;
  };
}

export const AddReadReplica: FC<AddRRProps> = (props) => {
  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const universeUUID = props.params?.uuid ?? '';
  const exitRoute = getReadReplicaExitRoute(universeUUID);

  const addRRContextData = useMethods(addRRFormMethods, {
    ...initialAddRRFormState,
    universeUuid: universeUUID
  });

  const [addRRContext, addRRMethods] = addRRContextData;
  const { activeStep, isEditPlacementOnly } = addRRContext;
  const steps = useMemo(
    () => getRRSteps(t, { editPlacementOnly: isEditPlacementOnly }),
    [t, isEditPlacementOnly]
  );
  const totalSubSteps = useMemo(
    () => steps.reduce((count, step) => count + step.subSteps.length, 0),
    [steps]
  );
  const isLastStep = activeStep === totalSubSteps;
  const currentStepRef = useRef<StepsRef>(null);

  const { isLoading: isUniverseDataLoading } = useQuery(
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
        <AddRRContext.Provider value={addRRContextData as unknown as AddRRContextProps}>
          <RRHeader>
            <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px', width: '100%' }}>
              <YBLogo />
              <Typography
                variant="h4"
                sx={{ color: '#1E154B', fontSize: '18px', fontWeight: 600, marginLeft: '16px' }}
              >
                {t(isEditPlacementOnly ? 'editPlacementTitle' : 'title')}
              </Typography>
            </Box>
            <Close
              style={{ cursor: 'pointer' }}
              onClick={() => {
                window.location.href = exitRoute;
              }}
            />
          </RRHeader>
          <Box sx={{ flex: 1, minHeight: 0, display: 'flex', overflow: 'hidden' }}>
            <Grid
              container
              spacing={{ xs: 3, md: 3, lg: 3, xl: 6 }}
              sx={{ flex: 1, minHeight: 0, width: '100%', flexWrap: 'nowrap' }}
            >
              <Grid
                sx={{
                  borderRight: '1px solid #E9EEF2',
                  overflowY: 'auto',
                  flexShrink: 0,
                  backgroundColor: '#FBFCFD'
                }}
                size="auto"
              >
                <YBMultiLevelStepper dataTestId="stepper" activeStep={activeStep} steps={steps} />
              </Grid>
              <Grid
                container
                direction={'column'}
                size="grow"
                spacing={0}
                sx={{ flex: 1, minHeight: 0, minWidth: 0 }}
              >
                <Grid size="grow" sx={{ minHeight: 0, overflowY: 'auto' }}>
                  <Box
                    sx={{
                      display: 'flex',
                      flexDirection: 'column',
                      maxWidth: '1024px',
                      minWidth: '856px',
                      width: '100%',
                      gap: 3,
                      mr: 1,
                      pb: 3
                    }}
                  >
                    <SwitchRRSteps ref={currentStepRef} />
                    {/* Footer */}
                    <Grid
                      container
                      display="flex"
                      alignItems="center"
                      justifyContent="space-between"
                      direction="row"
                    >
                      <YBButton
                        variant="secondary"
                        size="large"
                        dataTestId="add-rr-cancel-button"
                        onClick={() => {
                          window.location.href = exitRoute;
                        }}
                      >
                        {t('cancel', { keyPrefix: 'common' })}
                      </YBButton>
                      <Grid container alignItems="center" justifyContent="flex-end" spacing={2}>
                        {!isEditPlacementOnly && (
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
                        )}
                        <YBButton
                          onClick={() => {
                            currentStepRef.current?.onNext();
                          }}
                          variant="ybaPrimary"
                          size="large"
                          dataTestId="add-rr-next-button"
                        >
                          {isLastStep
                            ? isEditPlacementOnly
                              ? t('confirmAndApply')
                              : t('create', { keyPrefix: 'common' })
                            : t('next', { keyPrefix: 'common' })}
                        </YBButton>
                      </Grid>
                    </Grid>
                  </Box>
                </Grid>
              </Grid>
            </Grid>
          </Box>
        </AddRRContext.Provider>
      </AuthenticatedArea>
    </ReadReplicaRoot>
  );
};
