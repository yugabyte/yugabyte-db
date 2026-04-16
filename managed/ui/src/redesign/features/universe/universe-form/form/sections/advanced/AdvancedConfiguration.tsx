import { useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import {
  AccessKeysField,
  ARNField,
  ConnectionPoolingField,
  DBVersionField,
  DeploymentPortsField,
  IPV6Field,
  NetworkAccessField,
  PGCompatibiltyField,
  SystemDField
} from '../../fields';
import {
  CloudType,
  ClusterModes,
  ClusterType,
  RunTimeConfigEntry,
  UniverseFormConfigurationProps
} from '../../../utils/dto';
import { RuntimeConfigKey } from '../../../../../../helpers/constants';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';
import { UniverseFormContext } from '../../../UniverseFormContainer';

export const AdvancedConfiguration = ({ runtimeConfigs }: UniverseFormConfigurationProps) => {
  const classes = useSectionStyles();
  const { t } = useTranslation();

  //form context
  const { clusterType, mode, newUniverse }: any = useContext(UniverseFormContext)[0];

  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in create mode
  const isCreatePrimary = isCreateMode && isPrimary; //Editing Primary Cluster
  const isCreateRR = !newUniverse && isCreateMode && !isPrimary; //Adding Async Cluster to an existing Universe

  const isConnectionPoolEnabled =
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.ENABLE_CONNECTION_POOLING
    )?.value === 'true';

  // If use_ansible_provisioning is false, YNP is going to do the provisioning and YNP does not support cron based universes
  const useAnsibleProvisioning =
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.USE_ANSIBLE_PROVISIONING
    )?.value === 'true';

  //field data
  const provider = useWatch({ name: PROVIDER_FIELD });

  if (!provider?.code) return null;

  return (
    <Box
      className={classes.sectionContainer}
      flexDirection="column"
      data-testid="AdvancedConfiguration-Section"
    >
      <Typography variant="h4">{t('universeForm.advancedConfig.title')}</Typography>
      <Box display="flex" width="100%" mt={4}>
        <DBVersionField disabled={!isCreatePrimary} />
      </Box>
      {provider.code !== CloudType.kubernetes && (
        <Box display="flex" width="100%" mt={2}>
          <AccessKeysField disabled={!isCreatePrimary && !isCreateRR} isEditMode={!isCreateMode} />
        </Box>
      )}
      {provider.code === CloudType.aws && (
        <Box display="flex" width="100%" mt={2}>
          <ARNField disabled={!isPrimary} />
        </Box>
      )}
      {provider.code === CloudType.kubernetes && (
        <>
          <Box display="flex" width="100%" mt={2}>
            <IPV6Field disabled={!isCreatePrimary} />
          </Box>
          <Box display="flex" width="100%" mt={2}>
            <NetworkAccessField disabled={!isCreatePrimary} />
          </Box>
        </>
      )}
      {isPrimary && (
        <Box display="flex" width="100%" mt={2.5}>
          <PGCompatibiltyField disabled={!isCreateMode} />
        </Box>
      )}
      {isPrimary && isConnectionPoolEnabled && (
        <Box display="flex" width="100%" mt={2.5}>
          <ConnectionPoolingField disabled={!isCreateMode} />
        </Box>
      )}
      {provider.code !== CloudType.kubernetes && (
        <>
          {!isCreatePrimary && (
            <Box display="flex" width="100%" mt={2.5}>
              <SystemDField disabled={!useAnsibleProvisioning} />
            </Box>
          )}
          <Box display="flex" width="100%" mt={2.5}>
            <DeploymentPortsField
              disabled={provider.code === CloudType.kubernetes}
              isEditMode={!isCreateMode}
            />
          </Box>
        </>
      )}
    </Box>
  );
};
