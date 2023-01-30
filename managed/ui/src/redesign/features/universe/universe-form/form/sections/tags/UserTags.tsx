import React, { FC, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box, Grid, Typography } from '@material-ui/core';
import { UserTagsField } from '../../fields';
import { CloudType, ClusterType } from '../../../utils/dto';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';
import { UniverseFormContext } from '../../../UniverseFormContainer';

export const UserTags: FC = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();

  //form context
  const { clusterType } = useContext(UniverseFormContext)[0];

  //field data
  const provider = useWatch({ name: PROVIDER_FIELD });

  if (
    clusterType === ClusterType.PRIMARY &&
    [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code)
  )
    return (
      <Box className={classes.sectionContainer} data-testid="user-tags-section">
        <Typography className={classes.sectionHeaderFont}>
          {t('universeForm.userTags.title')}
        </Typography>
        <Box display="flex" width="100%" mt={2}>
          <Grid container item lg={6}>
            <UserTagsField />
          </Grid>
        </Box>
      </Box>
    );

  return null;
};
