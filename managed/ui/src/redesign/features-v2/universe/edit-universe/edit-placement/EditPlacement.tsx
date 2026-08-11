import { FC, useEffect, useMemo } from 'react';
import { mui, Step, YBMultiLevelStepper } from '@yugabyte-ui-library/core';

import { useTranslation } from 'react-i18next';
import { useMethods } from 'react-use';
import {
  EditPlacementSteps,
  EditPlacementContext,
  EditPlacementContextProps,
  editPlacementMethods
} from './EditPlacementContext';
import { EditPlacementNodesAndAvailability } from './EditPlacementNodesAndAvailability';
import { EditPlacementResilience } from './EditPlacementResilience';

import { useEditUniverseContext } from '../EditUniverseUtils';

import Close from '../../../../assets/close rounded inverted.svg';
import YBLogo from '../../../../assets/yb_logo.svg';
import { getResilienceAndRegionsProps } from './EditPlacementUtils';

const { styled, Grid2: Grid, Typography } = mui;

const EditPlacementRoot = styled('div')(() => ({
  backgroundColor: '#fff !important',
  position: 'absolute',
  top: 0,
  left: 0,
  zIndex: 1299,
  width: '100vw'
}));

interface EditPlacementProps {
  visible: boolean;
  onHide: () => void;
  skipResilienceAndRegionsStep?: boolean;
  selectedPartitionUUID?: string;
  onSubmit: (context: EditPlacementContextProps) => void;
}

export const EditPlacement: FC<EditPlacementProps> = ({
  visible,
  onHide,
  skipResilienceAndRegionsStep = false,
  selectedPartitionUUID,
  onSubmit
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });

  const editPlacementContext = useMethods(editPlacementMethods, {
    activeStep: EditPlacementSteps.RESILIENCE_AND_REGIONS
  });
  const { universeData, providerRegions } = useEditUniverseContext();
  const activeStep = editPlacementContext[0].activeStep;
  const { resetContext, setActiveStep, setResilience } = editPlacementContext[1];

  const steps = useMemo(() => {
    return [
      {
        groupTitle: t('placement'),
        subSteps: [
          {
            title: t('resilienceAndRegions')
          },
          {
            title: t('nodesAndAvailabilityZone')
          }
        ]
      }
    ] as Step[];
  }, []);

  useEffect(() => {
    if (skipResilienceAndRegionsStep) {
      const regions = getResilienceAndRegionsProps(
        universeData!,
        providerRegions,
        selectedPartitionUUID
      );
      setResilience(regions);
      setActiveStep(EditPlacementSteps.NODES_AND_AVAILABILITY_ZONES);
    }
  }, [visible, skipResilienceAndRegionsStep, selectedPartitionUUID]);

  if (!visible) {
    return null;
  }

  const hideModal = () => {
    resetContext();
    onHide();
  };

  return (
    <EditPlacementRoot>
      <EditPlacementContext.Provider
        value={
          ([
            ...editPlacementContext,
            { hideModal, selectedPartitionUUID, onSubmit }
          ] as unknown) as EditPlacementContextProps
        }
      >
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
              {t('title', { keyPrefix: 'editUniverse.editResilienceAndRegions' })}
            </Typography>
          </div>
          <Close style={{ cursor: 'pointer' }} onClick={hideModal} />
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
            {activeStep === EditPlacementSteps.RESILIENCE_AND_REGIONS ? (
              <EditPlacementResilience />
            ) : (
              <EditPlacementNodesAndAvailability />
            )}
          </Grid>
        </Grid>
      </EditPlacementContext.Provider>
    </EditPlacementRoot>
  );
};
