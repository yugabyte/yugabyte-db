import React, { FC, useContext } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { useWatch, useFormContext } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import { GFlagsField } from '../../fields';
import { ClusterModes, ClusterType, UniverseFormData } from '../../../utils/dto';
import { SOFTWARE_VERSION_FIELD, GFLAGS_FIELD } from '../../../utils/constants';
import { useSectionStyles } from '../../../universeMainStyle';
import { UniverseFormContext } from '../../../UniverseFormContainer';

export const GFlags: FC = () => {
  const classes = useSectionStyles();
  const { t } = useTranslation();

  //form context
  const { clusterType, mode } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isEditMode = mode === ClusterModes.EDIT; //Form is in edit mode
  const isEditPrimary = isEditMode && isPrimary; //Editing Primary Cluster

  //form Data
  const { control, getValues } = useFormContext<Partial<UniverseFormData>>();
  const dbVersion = useWatch({ name: SOFTWARE_VERSION_FIELD });

  if (!isPrimary || (isEditPrimary && _.isEmpty(getValues(GFLAGS_FIELD)))) return null;

  return (
    <Box className={classes.sectionContainer} data-testid="gflags-section">
      <Typography className={classes.sectionHeaderFont}>
        {t('universeForm.gFlags.title')}
      </Typography>
      <Box display="flex" width="100%" mt={2}>
        <GFlagsField
          control={control}
          dbVersion={dbVersion}
          editMode={false}
          fieldPath={GFLAGS_FIELD}
          isReadOnly={isEditPrimary}
        />
      </Box>
    </Box>
  );
};
