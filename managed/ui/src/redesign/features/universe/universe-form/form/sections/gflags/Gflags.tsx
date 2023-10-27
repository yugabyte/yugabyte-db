import React, { FC, useContext, useState } from 'react';
import _ from 'lodash';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { useWatch, useFormContext } from 'react-hook-form';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { GFlagsField } from '../../fields';
import { YBToggleField } from '../../../../../../components';
import { ReadOnlyGflagsModal } from './ReadOnlyGflagsModal';
import {
  CloudType,
  ClusterModes,
  ClusterType,
  RunTimeConfigEntry,
  UniverseFormConfigurationProps,
  UniverseFormData
} from '../../../utils/dto';
import {
  PROVIDER_FIELD,
  SOFTWARE_VERSION_FIELD,
  GFLAGS_FIELD,
  INHERIT_FLAGS_FROM_PRIMARY
} from '../../../utils/constants';
import { RuntimeConfigKey } from '../../../../../../helpers/constants';
import { useSectionStyles } from '../../../universeMainStyle';
import { UniverseFormContext } from '../../../UniverseFormContainer';

const useStyles = makeStyles((theme) => ({
  inheritFlagsContainer: {
    display: 'flex',
    flexDirection: 'row',
    width: '660px',
    height: '68px',
    border: '1px solid #E5E5E9',
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2)
  },
  defaultBox: {
    height: '20px',
    width: '50px',
    padding: theme.spacing(0.25, 0.75),
    background: '#F0F4F7',
    border: '1px solid #E9EEF2',
    borderRadius: theme.spacing(0.5),
    marginLeft: theme.spacing(1),
    marginRight: theme.spacing(2.5)
  },
  primaryFlagsLink: {
    textDecoration: 'underline',
    fontSize: '12px',
    fontWeight: 400,
    color: '#67666C',
    cursor: 'pointer'
  },
  noteText: {
    fontSize: '12px',
    fontWeight: 400,
    color: '#67666C'
  }
}));

export const GFlags = ({ runtimeConfigs }: UniverseFormConfigurationProps) => {
  const classes = useSectionStyles();
  const gflagClasses = useStyles();
  const { t } = useTranslation();
  const [openReadOnlyModal, setReadOnlyModal] = useState(false);
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const enableRRGflags = featureFlags.test.enableRRGflags || featureFlags.released.enableRRGflags;
  //form context
  const { clusterType, mode, primaryFormData } = useContext(UniverseFormContext)[0];
  const isPrimary = clusterType === ClusterType.PRIMARY;
  const isEditMode = mode === ClusterModes.EDIT; //Form is in edit mode
  const isEditPrimary = isEditMode && isPrimary; //Editing Primary Cluster

  // Value of runtime config key
  const isGFlagMultilineConfEnabled =
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.IS_GFLAG_MULTILINE_ENABLED
    )?.value === 'true';

  //form Data
  const { control, getValues } = useFormContext<Partial<UniverseFormData>>();
  const provider = useWatch({ name: PROVIDER_FIELD });
  const dbVersion = useWatch({ name: SOFTWARE_VERSION_FIELD });
  const isInherited = useWatch({ name: INHERIT_FLAGS_FROM_PRIMARY });

  if (isEditPrimary && _.isEmpty(getValues(GFLAGS_FIELD))) return null;
  if (!enableRRGflags && !isPrimary) return null;

  return (
    <Box className={classes.sectionContainer} flexDirection="column" data-testid="Gflags-Section">
      <Typography variant="h4">{t('universeForm.gFlags.title')}</Typography>
      {!isPrimary && enableRRGflags && (
        <Box className={gflagClasses.inheritFlagsContainer}>
          <Box flexShrink={1}>
            <YBToggleField
              name={INHERIT_FLAGS_FROM_PRIMARY}
              disabled={isEditMode}
              inputProps={{
                'data-testid': 'ToggleInheritFlags'
              }}
              control={control}
            />
          </Box>
          <Box display="flex" flexDirection="column">
            <Box display="flex" flex={1} flexDirection={'row'}>
              <Typography variant="body2">{t('universeForm.gFlags.inheritFlagsMsg')}</Typography>
              <span className={gflagClasses.defaultBox}>
                <Typography variant="subtitle1">Default</Typography>
              </span>
              <Typography
                className={gflagClasses.primaryFlagsLink}
                onClick={() => setReadOnlyModal(true)}
              >
                {t('universeForm.gFlags.primaryClusterFlags')}
              </Typography>
            </Box>
            <Box display="flex">
              <Typography className={gflagClasses.noteText}>
                <b>{t('universeForm.gFlags.note')}</b> &nbsp;{' '}
                {t('universeForm.gFlags.rrWithOnlyTserver')}
              </Typography>
            </Box>
          </Box>
        </Box>
      )}

      {(isPrimary ||
        (enableRRGflags &&
          !isInherited &&
          !isPrimary &&
          provider?.code !== CloudType.kubernetes)) && (
        <Box display="flex" width={isPrimary ? '1284px' : '1062px'} mt={4}>
          <GFlagsField
            control={control}
            dbVersion={dbVersion}
            editMode={false}
            fieldPath={GFLAGS_FIELD}
            isReadReplica={!isPrimary}
            isReadOnly={isEditMode}
            isGFlagMultilineConfEnabled={isGFlagMultilineConfEnabled}
          />
        </Box>
      )}
      <ReadOnlyGflagsModal
        open={openReadOnlyModal}
        onClose={() => setReadOnlyModal(false)}
        gFlags={primaryFormData?.gFlags}
      />
    </Box>
  );
};
