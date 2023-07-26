import { FC, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import {
  AccessKeysField,
  ARNField,
  DBVersionField,
  DeploymentPortsField,
  IPV6Field,
  NetworkAccessField,
  SystemDField
} from '../../fields';
import { CloudType, ClusterModes, ClusterType } from '../../../utils/dto';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';
import { UniverseFormContext } from '../../../UniverseFormContainer';

export const AdvancedConfiguration: FC = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();

  //form context
  const { clusterType, mode, newUniverse }: any = useContext(UniverseFormContext)[0];

  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in create mode
  const isCreatePrimary = isCreateMode && isPrimary; //Editing Primary Cluster
  const isCreateRR = !newUniverse && isCreateMode && !isPrimary; //Adding Async Cluster to an existing Universe

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
          <AccessKeysField disabled={!isCreatePrimary && !isCreateRR} />
        </Box>
      )}
      {provider.code === CloudType.aws && (
        <Box display="flex" width="100%" mt={2}>
          <ARNField disabled={!isCreatePrimary && !isCreateRR} />
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
      {provider.code !== CloudType.kubernetes && (
        <>
          <Box display="flex" width="100%" mt={2.5}>
            <SystemDField disabled={!isCreatePrimary} />
          </Box>
          <Box display="flex" width="100%" mt={2.5}>
            <DeploymentPortsField disabled={!isCreatePrimary} isEditMode={!isCreateMode} />
          </Box>
        </>
      )}
    </Box>
  );
};
