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

import { isKubernetesUniverse, useEditUniverseContext } from '../EditUniverseUtils';

import Close from '../../../../assets/close rounded inverted.svg';
import YBLogo from '../../../../assets/yb_logo.svg';
import {
  getNodesAvailabilityDefaultsForEditPlacement,
  getResilienceAndRegionsProps
} from './EditPlacementUtils';

const { styled, Grid2: Grid, Typography, Box } = mui;

const EditPlacementRoot = styled('div')(() => ({
  backgroundColor: '#fff !important',
  display: 'flex',
  inset: 0,
  flexDirection: 'column',
  position: 'fixed',
  overflow: 'hidden',
  zIndex: 1299
}));

const EditPlacementsHeader = styled('div')(() => ({
  display: 'flex',
  flexDirection: 'row',
  padding: '8px 24px 8px 20px',
  height: '64px',
  backgroundColor: '#E9EEF2',
  alignItems: 'center',
  justifyContent: 'space-between',
  minWidth: '1200px'
}));

interface EditPlacementProps {
  visible: boolean;
  onHide: () => void;
  skipResilienceAndRegionsStep?: boolean;
  selectedPartitionUUID?: string;
  isSubmittingPlacementUpdate?: boolean;
  onSubmit: (context: EditPlacementContextProps, onSuccess?: () => void) => void;
}

export const EditPlacement: FC<EditPlacementProps> = ({
  visible,
  onHide,
  skipResilienceAndRegionsStep = false,
  selectedPartitionUUID,
  isSubmittingPlacementUpdate = false,
  onSubmit
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.steps' });

  const editPlacementContext = useMethods(editPlacementMethods, {
    activeStep: EditPlacementSteps.RESILIENCE_AND_REGIONS
  });
  const { universeData, providerRegions } = useEditUniverseContext();
  const isK8s = isKubernetesUniverse(universeData!);
  const activeStep = editPlacementContext[0].activeStep;
  const { resetContext, setActiveStep, setNodesAndAvailability, setResilience } =
    editPlacementContext[1];

  const steps = useMemo(() => {
    return [
      {
        groupTitle: t('placement'),
        subSteps: [
          {
            title: t('resilienceAndRegions')
          },
          {
            title: t(isK8s ? 'podsAndAvailabilityZone' : 'nodesAndAvailabilityZone')
          }
        ]
      }
    ] as Step[];
  }, [t, isK8s]);

  useEffect(() => {
    if (!visible) {
      resetContext();
    }
  }, [visible]);

  useEffect(() => {
    if (!visible) {
      return;
    }
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => {
      document.body.style.overflow = previousOverflow;
    };
  }, [visible]);

  useEffect(() => {
    if (skipResilienceAndRegionsStep && visible) {
      const regions = getResilienceAndRegionsProps(
        universeData!,
        providerRegions,
        selectedPartitionUUID
      );
      const nodesAndAvailability = getNodesAvailabilityDefaultsForEditPlacement(
        universeData!,
        selectedPartitionUUID
      );
      setResilience(regions);
      setNodesAndAvailability(nodesAndAvailability);
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
          [
            ...editPlacementContext,
            { hideModal, selectedPartitionUUID, isSubmittingPlacementUpdate, onSubmit }
          ] as unknown as EditPlacementContextProps
        }
      >
        <EditPlacementsHeader>
          <Box sx={{ display: 'flex', flexDirection: 'row', gap: '16px', width: '100%' }}>
            <YBLogo />
            <Typography
              variant="h4"
              sx={{ color: '#1E154B', fontSize: '18px', fontWeight: 600, lineHeight: '24px' }}
            >
              {t('title', { keyPrefix: 'editUniverse.editResilienceAndRegions' })}
            </Typography>
          </Box>
          <Close style={{ cursor: 'pointer' }} onClick={hideModal} />
        </EditPlacementsHeader>
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
              <YBMultiLevelStepper
                dataTestId="edit-placemment-stepper"
                activeStep={activeStep}
                steps={steps}
              />
            </Grid>
            <Grid
              container
              direction="column"
              size="grow"
              spacing={0}
              sx={{ flex: 1, minHeight: 0, minWidth: 0 }}
            >
              <Grid size="grow" sx={{ minHeight: 0, overflowY: 'auto' }}>
                <Box
                  sx={{
                    display: 'flex',
                    flexDirection: 'column',
                    maxWidth: '1114px',
                    minWidth: '856px',
                    width: '100%',
                    gap: 3,
                    mr: 1,
                    pb: 3
                  }}
                >
                  {activeStep === EditPlacementSteps.RESILIENCE_AND_REGIONS ? (
                    <EditPlacementResilience />
                  ) : (
                    <EditPlacementNodesAndAvailability />
                  )}
                </Box>
              </Grid>
            </Grid>
          </Grid>
        </Box>
      </EditPlacementContext.Provider>
    </EditPlacementRoot>
  );
};
