import { FC } from 'react';
import { mui, yba } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { DedicatedNode } from '../../create-universe/steps/nodes-availability/DedicatedNodes';
import {
  CreateUniverseContext,
  createUniverseFormProps
} from '../../create-universe/CreateUniverseContext';
import { EditPlacementContextProps } from '../edit-placement/EditPlacementContext';
import {
  countRegionsAzsAndNodes,
  getClusterByType,
  mapUniversePayloadToResilienceAndRegionsProps,
  useEditUniverseContext
} from '../EditUniverseUtils';
import { FormProvider, useForm } from 'react-hook-form';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import EditIcon from '@app/redesign/assets/edit2.svg';

interface MasterServerNodeAllocationModalProps {
  visible: boolean;
  onClose: () => void;
}

const { YBModal } = yba;
const { styled, Box, Typography } = mui;

const StyledPanel = styled('div')(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  flexDirection: 'column',
  gap: '0px'
}));

const StyledEditMasterServerPanel = styled('div')(({ theme }) => ({
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  display: 'flex',
  flexDirection: 'column',
  marginLeft: '58px',
  marginBottom: '24px',
  padding: '8px 16px',
  marginRight: '24px'
}));

const Header = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'row',
  gap: '4px',
  padding: '10px 8px',
  alignItems: 'center',
  color: theme.palette.primary[600],
  cursor: 'pointer'
}));

export const MasterServerNodeAllocationModal: FC<MasterServerNodeAllocationModalProps> = ({
  visible,
  onClose
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.masterServerNodeAllocationModal'
  });
  const { universeData, providerRegions } = useEditUniverseContext();

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const stats = countRegionsAzsAndNodes(primaryCluster!.placement_spec!);

  const regions = mapUniversePayloadToResilienceAndRegionsProps(
    providerRegions!,
    stats,
    primaryCluster!
  );
  const methods = useForm<NodeAvailabilityProps>({
    defaultValues: {
      useDedicatedNodes: true
    }
  });

  const { watch } = methods;
  const enableDedicatedNodes = watch('useDedicatedNodes');

  if (!visible) return null;

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      title={t('title')}
      submitLabel={t('applyChanges', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      size="md"
      dialogContentProps={{
        sx: {
          padding: '24px 16px !important'
        }
      }}
    >
      <CreateUniverseContext.Provider
        value={
          ([
            {
              activeStep: 1,
              resilienceAndRegionsSettings: regions
            },
            {
              saveNodesAvailabilitySettings: (
                data: EditPlacementContextProps['nodesAndAvailability']
              ) => {},
              moveToNextPage: () => {}
            }
          ] as unknown) as createUniverseFormProps
        }
      >
        <FormProvider {...methods}>
          <StyledPanel>
            <div>
              <DedicatedNode noAccordion />
            </div>
            {enableDedicatedNodes && (
              <StyledEditMasterServerPanel>
                <Header>
                  <EditIcon />
                  <Typography variant="body1">{t('editMasterServer')}</Typography>
                </Header>
                <Box
                  sx={(theme) => {
                    return {
                      marginLeft: '32px',
                      display: 'flex',
                      flexDirection: 'column',
                      gap: '2px',
                      color: theme.palette.grey[700]
                    };
                  }}
                >
                  <Typography variant="subtitle1">{t('subText')}</Typography>
                  <Typography variant="subtitle2" fontWeight={600}>
                    {t('settingsAndHardware')}
                  </Typography>
                </Box>
              </StyledEditMasterServerPanel>
            )}
          </StyledPanel>
        </FormProvider>
      </CreateUniverseContext.Provider>
    </YBModal>
  );
};
