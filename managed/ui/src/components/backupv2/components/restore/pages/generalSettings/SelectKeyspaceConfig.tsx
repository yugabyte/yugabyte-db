/*
 * Created on Wed Jun 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext } from 'react';
import Select from 'react-select';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { Controller, useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useMount } from 'react-use';
import { IGeneralSettings } from './GeneralSettings';
import { YBLabel } from '../../../../../common/descriptors';
import { YBCheckboxField } from '../../../../../../redesign/components';
import { RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import Checked from '../../icons/checkbox/Checked.svg';
import UnChecked from '../../icons/checkbox/UnChecked.svg';

const useStyles = makeStyles((theme) => ({
  controls: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2),
    marginTop: theme.spacing(1)
  },
  renameKeyspace: {
    marginTop: theme.spacing(2)
  },
  renameKeyspaceHelperText: {
    color: '#67666C',
    fontSize: '12px',
    display: 'inline-block',
    marginLeft: theme.spacing(4.25)
  }
}));

const SelectKeyspaceConfig = () => {
  const classes = useStyles();

  const [
    {
      backupDetails,
      formData: { generalSettings }
    }
  ]: RestoreContextMethods = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const { control, watch, setValue } = useFormContext<IGeneralSettings>();
  const { t } = useTranslation();

  const forceKeyspaceRename = watch('forceKeyspaceRename');
  const renameKeyspace = watch('renameKeyspace');

  useMount(() => {
    if (
      !generalSettings?.incrementalBackupProps.isRestoreEntireBackup &&
      generalSettings?.incrementalBackupProps.singleKeyspaceRestore
    ) {
      setValue('selectedKeyspace', {
        label: backupDetails!.commonBackupInfo.responseList[0].keyspace,
        value: backupDetails!.commonBackupInfo.responseList[0].keyspace
      });
    } else {
      setValue('selectedKeyspace', {
        label: t('newRestoreModal.allKeyspaces'),
        value: t('newRestoreModal.allKeyspaces')
      });
    }
  });

  return (
    <Box>
      <Typography variant="body1">
        {t('newRestoreModal.generalSettings.selectKeyspaceForm.title')}
      </Typography>
      <div className={classes.controls}>
        <Controller
          control={control}
          name="selectedKeyspace"
          render={({ field: { value } }) => (
            <YBLabel label={t('newRestoreModal.generalSettings.selectKeyspaceForm.selectKeyspace')}>
              <Select options={[]} isDisabled value={value} />
            </YBLabel>
          )}
        />
        <div className={classes.renameKeyspace}>
          <YBCheckboxField
            label={t('newRestoreModal.generalSettings.selectKeyspaceForm.restoreKeyspace', {
              Optional: !forceKeyspaceRename
                ? t('newRestoreModal.generalSettings.selectKeyspaceForm.optional')
                : ''
            })}
            checked={renameKeyspace}
            control={control}
            name="renameKeyspace"
            disabled={forceKeyspaceRename}
            icon={<img src={UnChecked} alt="unchecked" />}
            checkedIcon={<img src={Checked} alt="checked" />}
          />
          {forceKeyspaceRename && (
            <span className={classes.renameKeyspaceHelperText}>
              <Trans
                i18nKey="newRestoreModal.generalSettings.selectKeyspaceForm.forceRenameHelpText"
                components={{ b: <b /> }}
              />
            </span>
          )}
        </div>
      </div>
    </Box>
  );
};

export default SelectKeyspaceConfig;
