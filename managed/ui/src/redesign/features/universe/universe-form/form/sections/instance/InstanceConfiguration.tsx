import { useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, Grid, Typography, makeStyles } from '@material-ui/core';
import {
  InstanceTypeField,
  K8NodeSpecField,
  K8VolumeInfoField,
  VolumeInfoField,
  StorageTypeField
} from '../../fields';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import {
  CloudType,
  ClusterModes,
  ClusterType,
  MasterPlacementMode,
  RunTimeConfigEntry,
  UniverseFormData,
  UniverseFormConfigurationProps
} from '../../../utils/dto';
import {
  PROVIDER_FIELD,
  MASTER_PLACEMENT_FIELD,
  DEVICE_INFO_FIELD
} from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';

const CONTAINER_WIDTH = '605px';

const useStyles = makeStyles((theme) => ({
  settingsContainer: {
    backgroundColor: theme.palette.common.white,
    border: '1px solid #E5E5E6',
    width: CONTAINER_WIDTH,
    borderRadius: theme.spacing(1),
    marginRight: theme.spacing(2),
    flexShrink: 1
  },
  infoTooltipIcon: {
    marginLeft: theme.spacing(1)
  }
}));

export const InstanceConfiguration = ({ runtimeConfigs }: UniverseFormConfigurationProps) => {
  const classes = useSectionStyles();
  const helperClasses = useStyles();
  const { t } = useTranslation();

  // Value of runtime config key
  const useK8CustomResourcesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.use_k8s_custom_resources'
  );
  const useK8CustomResources = !!(useK8CustomResourcesObject?.value === 'true');
  const maxVolumeCount = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.max_volume_count'
  )?.value;

  //form context
  const { getValues } = useFormContext<UniverseFormData>();
  const { mode, clusterType, newUniverse, universeConfigureTemplate } = useContext(
    UniverseFormContext
  )[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in edit mode
  const isCreatePrimary = isCreateMode && isPrimary; //Creating Primary Cluster
  const isCreateRR = !newUniverse && isCreateMode && !isPrimary; //Adding Async Cluster to an existing Universe
  const isNodeResizable = !isCreateMode ? universeConfigureTemplate?.nodesResizeAvailable : true;
  //field data
  const provider = useWatch({ name: PROVIDER_FIELD });
  const deviceInfo = useWatch({ name: DEVICE_INFO_FIELD });
  const masterPlacement = isPrimary
    ? useWatch({ name: MASTER_PLACEMENT_FIELD })
    : getValues(MASTER_PLACEMENT_FIELD);

  // Wrapper elements to get instance metadata and dedicated container element
  const getInstanceMetadataElement = (isDedicatedMasterField: boolean) => {
    return (
      <Box width={masterPlacement === MasterPlacementMode.DEDICATED ? '100%' : CONTAINER_WIDTH}>
        {provider?.code === CloudType.kubernetes && useK8CustomResources ? (
          <>
            <K8NodeSpecField
              isEditMode={!isCreateMode}
              isDedicatedMasterField={isDedicatedMasterField}
            />
            <K8VolumeInfoField
              isEditMode={!isCreateMode}
              isDedicatedMasterField={isDedicatedMasterField}
              disableVolumeSize={!isNodeResizable}
              disableNumVolumes={!isCreateMode && provider?.code === CloudType.kubernetes}
              maxVolumeCount={maxVolumeCount}
            />
          </>
        ) : (
          <>
            <InstanceTypeField
              isEditMode={!isCreateMode}
              isDedicatedMasterField={isDedicatedMasterField}
            />
            <VolumeInfoField
              isEditMode={!isCreateMode}
              isPrimary={isPrimary}
              disableVolumeSize={!isNodeResizable}
              disableNumVolumes={!isCreateMode && provider?.code === CloudType.kubernetes}
              disableStorageType={!isCreatePrimary && !isCreateRR}
              disableIops={!isCreatePrimary && !isCreateRR}
              disableThroughput={!isCreatePrimary && !isCreateRR}
              isDedicatedMasterField={isDedicatedMasterField}
              maxVolumeCount={maxVolumeCount}
            />
          </>
        )}
      </Box>
    );
  };
  const getDedicatedContainerElement = (instanceLabel: string, isDedicatedMasterField: boolean) => {
    return (
      <Box className={helperClasses.settingsContainer}>
        <Box m={2}>
          <Typography className={classes.subsectionHeaderFont}>{t(instanceLabel)}</Typography>
          {getInstanceMetadataElement(isDedicatedMasterField)}
        </Box>
      </Box>
    );
  };

  return (
    <Box
      className={classes.sectionContainer}
      flexDirection="column"
      data-testid="InstanceConfiguration-Section"
    >
      <Typography variant="h4">{t('universeForm.instanceConfig.title')}</Typography>
      <Box width="100%" display="flex" flexDirection="column" mt={4}>
        <Grid container spacing={3}>
          <Grid lg={6} item container>
            {/* Display separate section for Master and TServer in dedicated mode*/}
            <Box flex={1} display="flex" flexDirection="row">
              {masterPlacement === MasterPlacementMode.COLOCATED
                ? getInstanceMetadataElement(false)
                : getDedicatedContainerElement('universeForm.tserver', false)}
              {masterPlacement === MasterPlacementMode.DEDICATED &&
                getDedicatedContainerElement('universeForm.master', true)}
            </Box>
          </Grid>
        </Grid>

        {/* Display storage type separately in case of GCP outside the Instance config container */}
        {deviceInfo &&
          provider?.code === CloudType.gcp &&
          masterPlacement === MasterPlacementMode.DEDICATED && (
            <Box width="50%">
              <StorageTypeField disableStorageType={!isCreatePrimary && !isCreateRR} />
            </Box>
          )}
      </Box>
    </Box>
  );
};
