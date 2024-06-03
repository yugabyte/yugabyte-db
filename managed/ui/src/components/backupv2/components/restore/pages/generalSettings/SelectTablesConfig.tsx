/*
 * Created on Mon Jun 26 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Typography, makeStyles } from '@material-ui/core';
import { RadioGroupOrientation, YBRadioGroupField } from '../../../../../../redesign/components';
import { IGeneralSettings } from './GeneralSettings';
import { RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { TableType } from '../../../../../../redesign/helpers/dtos';
import { isDefinedNotNull } from '../../../../../../utils/ObjectUtils';
import { isSelectiveRestoreSupported } from '../../RestoreUtils';

const useStyles = makeStyles((theme) => ({
  root: {},

  controls: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2),
    marginTop: theme.spacing(1),
    '& .MuiFormControlLabel-root:not(:last-child)': {
      marginBottom: theme.spacing(3)
    }
  }
}));

export const SelectTablesConfig = () => {
  const { control } = useFormContext<IGeneralSettings>();
  const { t } = useTranslation();
  const classes = useStyles();

  const [
    {
      backupDetails,
      formData: { preflightResponse, generalSettings }
    }
  ] = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const options: { value: IGeneralSettings['tableSelectionType']; label: string }[] = [
    {
      value: 'ALL_TABLES',
      label: t('newRestoreModal.generalSettings.tablesSelect.selectAllTables')
    },
    {
      value: 'SUBSET_OF_TABLES',
      label: t('newRestoreModal.generalSettings.tablesSelect.selectSubsetOfTables')
    }
  ];

  if (
    backupDetails?.backupType !== TableType.YQL_TABLE_TYPE ||
    generalSettings?.incrementalBackupProps.singleKeyspaceRestore !== true ||
    !isDefinedNotNull(preflightResponse)
  ) {
    return null;
  }

  if (!isSelectiveRestoreSupported(preflightResponse!)) {
    return null;
  }

  return (
    <div className={classes.root}>
      <Typography variant="body1">
        {t('newRestoreModal.generalSettings.tablesSelect.title')}
      </Typography>
      <div className={classes.controls}>
        <YBRadioGroupField
          control={control}
          options={options}
          name="tableSelectionType"
          orientation={RadioGroupOrientation.VERTICAL}
        />
      </div>
    </div>
  );
};
