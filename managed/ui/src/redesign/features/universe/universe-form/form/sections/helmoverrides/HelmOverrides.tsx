import { FC, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import { HelmOverridesField } from '../../fields';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import { CloudType, ClusterType, ClusterModes } from '../../../utils/dto';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';

export const HelmOverrides: FC = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();

  //form context
  const { mode, clusterType, universeConfigureTemplate } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isCreateMode = mode === ClusterModes.CREATE; //Form is in create mode
  const isCreatePrimary = isPrimary && isCreateMode;

  //field data
  const provider = useWatch({ name: PROVIDER_FIELD });

  if (isCreatePrimary && provider?.code === CloudType.kubernetes)
    return (
      <Box data-testid="HelmOverrides-Section">
        <Box mt={2}>
          <Typography className={classes.sectionHeaderFont}>
            {t('universeForm.helmOverrides.title')}
          </Typography>
        </Box>
        <Box>
          <HelmOverridesField
            disabled={false}
            universeConfigureTemplate={universeConfigureTemplate}
          />
        </Box>
      </Box>
    );

  return null;
};
