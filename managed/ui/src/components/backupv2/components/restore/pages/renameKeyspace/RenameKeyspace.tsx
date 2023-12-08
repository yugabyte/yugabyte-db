/*
 * Created on Tue Jun 27 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useCallback, useContext, useImperativeHandle, useState } from 'react';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Grid, Typography, makeStyles } from '@material-ui/core';
import { differenceWith, intersectionWith, uniqBy, values } from 'lodash';
import { useMount } from 'react-use';
import { useQuery } from 'react-query';
import { yupResolver } from '@hookform/resolvers/yup';
import { BackupRestoreStepper } from '../../common/BackupRestoreStepper';
import { PageRef, RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { YBSearchInput } from '../../../../../common/forms/fields/YBSearchInput';
import { PerLocationBackupInfo } from '../../api';
import { YBInput, YBInputField } from '../../../../../../redesign/components';
import { fetchTablesInUniverse } from '../../../../../../actions/xClusterReplication';
import { getValidationSchema } from './ValidationSchema';
import { isNonEmptyString } from '../../../../../../utils/ObjectUtils';
import { ITable } from '../../../../common/IBackup';
import { YBLoadingCircleIcon } from '../../../../../common/indicators';

export type IRenameKeyspace = {
  renamedKeyspaces: Array<PerLocationBackupInfo & { renamedKeyspace?: string }>;
};

const useStyles = makeStyles((theme) => ({
  searchCtrl: {
    marginBottom: theme.spacing(2),
    width: '100%'
  },
  searchHeader: {
    marginBottom: theme.spacing(2)
  },
  renameInputCtrls: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2)
  },
  keyspaceRow: {
    marginBottom: theme.spacing(3),
    gap: theme.spacing(1)
  },
  currentKeyspaceName: {
    width: '520px',
    '& .MuiInput-root': {
      background: '#F7F7F7'
    }
  },
  newKeyspaceName: {
    width: '500px'
  },
  tableLoadingMsg: {
    textAlign: 'center'
  },
  tableHeader: {
    marginBottom: theme.spacing(1),
    display: 'flex',
    gap: theme.spacing(2),
    '&>div': {
      width: '520px'
    }
  },
  stepper: {
    marginBottom: theme.spacing(3)
  }
}));

// eslint-disable-next-line react/display-name
const RenameKeyspace = React.forwardRef<PageRef>((_, forwardRef) => {
  const restoreContext = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const [
    {
      backupDetails,
      formData: { generalSettings, preflightResponse, renamedKeyspaces }
    },
    { moveToNextPage, moveToPrevPage, setSubmitLabel, saveRenamedKeyspacesFormData }
  ] = restoreContext;

  const { t } = useTranslation();

  const isTableByTableBackup = backupDetails?.commonBackupInfo.tableByTableBackup;

  const saveValues = useCallback(
    (val: IRenameKeyspace) => {
      const renamedKeyspaceValues = { ...val };

      if (isTableByTableBackup) {
        // In table by table backup, there will be multiple keyspace group.
        // But, we display only one keyspaces for a group to the user.
        // if it is renamed, we then updated the name to all keyspaces in that group

        const renameKeyspaceMap = {};
        //collect the keyspaces which are renamed
        val.renamedKeyspaces.forEach((r) => {
          if (isNonEmptyString(r.renamedKeyspace)) {
            renameKeyspaceMap[r.perBackupLocationKeyspaceTables.originalKeyspace] =
              r.renamedKeyspace;
          }
        });

        renamedKeyspaceValues.renamedKeyspaces = val.renamedKeyspaces.map((r) => ({
          ...r,
          renamedKeyspace:
            renameKeyspaceMap[r.perBackupLocationKeyspaceTables.originalKeyspace] ??
            r.perBackupLocationKeyspaceTables.originalKeyspace
        }));
      }

      saveRenamedKeyspacesFormData(renamedKeyspaceValues);
      moveToNextPage();
    },
    [moveToNextPage, saveRenamedKeyspacesFormData]
  );

  const { data: tablesInTargetUniverse, isLoading } = useQuery(
    ['tables', generalSettings?.targetUniverse?.value],
    () => fetchTablesInUniverse(generalSettings?.targetUniverse?.value),
    {
      refetchOnMount: false,
      select(data) {
        return data.data;
      }
    }
  );

  const keyspacesAvailableInTargetUniverse: string[] =
    tablesInTargetUniverse
      ?.filter((t: ITable) => t.tableType === (backupDetails?.backupType as any))
      .map((t: ITable) => t.keySpace) ?? [];

  // move the keyspaces which are duplicate to the top.
  let sortByTableDuplicate = [
    ...intersectionWith(
      values(preflightResponse?.perLocationBackupInfoMap),
      keyspacesAvailableInTargetUniverse,
      (infoMap, keyspace) => {
        return keyspace === infoMap.perBackupLocationKeyspaceTables.originalKeyspace;
      }
    ),
    ...differenceWith(
      values(preflightResponse?.perLocationBackupInfoMap),
      keyspacesAvailableInTargetUniverse,
      (infoMap, keyspace) => {
        return keyspace === infoMap.perBackupLocationKeyspaceTables.originalKeyspace;
      }
    )
  ];

  if (isTableByTableBackup) {
    sortByTableDuplicate = uniqBy(
      sortByTableDuplicate,
      (e) => e.perBackupLocationKeyspaceTables.originalKeyspace
    );
  }

  const methods = useForm<IRenameKeyspace>({
    defaultValues: {
      renamedKeyspaces:
        renamedKeyspaces.renamedKeyspaces.length > 0
          ? renamedKeyspaces.renamedKeyspaces
          : sortByTableDuplicate
    },
    resolver: yupResolver(
      getValidationSchema(restoreContext[0], keyspacesAvailableInTargetUniverse, t),
      { context: {} }
    )
  });

  const { handleSubmit, control } = methods;

  const classes = useStyles();
  const [searchText, setSearchText] = useState('');

  const onNext = useCallback(() => handleSubmit(saveValues)(), [handleSubmit, saveValues]);
  const onPrev = useCallback(() => moveToPrevPage(), [moveToPrevPage]);

  useImperativeHandle(forwardRef, () => ({ onNext, onPrev }), [onNext]);

  useMount(() => {
    if (generalSettings?.tableSelectionType === 'SUBSET_OF_TABLES') {
      setSubmitLabel(t('newRestoreModal.generalSettings.next'));
    } else {
      setSubmitLabel(t('newRestoreModal.generalSettings.restore'));
    }
  });

  const forceKeyspaceRename = generalSettings?.forceKeyspaceRename;

  if (isLoading) {
    return (
      <div className={classes.tableLoadingMsg}>
        <YBLoadingCircleIcon />
        {t('newRestoreModal.renameKeyspaces.loadingMsg')}
      </div>
    );
  }

  return (
    <Box>
      <BackupRestoreStepper className={classes.stepper} />
      <Box className={classes.searchCtrl}>
        <Typography variant="body2" className={classes.searchHeader}>
          {t('newRestoreModal.generalSettings.selectKeyspaceForm.restoreKeyspace', {
            Optional: !forceKeyspaceRename
              ? t('newRestoreModal.generalSettings.selectKeyspaceForm.optional')
              : ''
          })}
        </Typography>
        <YBSearchInput
          val={searchText}
          onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
            setSearchText(e.target.value);
          }}
          placeHolder={t('newRestoreModal.selectTables.searchTableName')}
        />
      </Box>
      <div className={classes.renameInputCtrls}>
        <div className={classes.tableHeader}>
          <div>{t('newRestoreModal.renameKeyspaces.currenyKeyspacename')}</div>
          <div>{t('newRestoreModal.renameKeyspaces.assignNewName')}</div>
        </div>
        <Grid container>
          {preflightResponse &&
            sortByTableDuplicate.map((e, ind) => {
              const origKeyspaceName = e.perBackupLocationKeyspaceTables.originalKeyspace;
              if (searchText) {
                if (!origKeyspaceName.includes(searchText)) {
                  return null;
                }
              }
              return (
                <Grid item xs={12} container className={classes.keyspaceRow} key={ind} spacing={1}>
                  <Grid item xs={6}>
                    <YBInput
                      value={origKeyspaceName}
                      error={keyspacesAvailableInTargetUniverse.includes(origKeyspaceName)}
                      helperText={
                        keyspacesAvailableInTargetUniverse.includes(origKeyspaceName) &&
                        t(
                          'newRestoreModal.renameKeyspaces.validationMessages.keyspacesAlreadyExists'
                        )
                      }
                      className={classes.currentKeyspaceName}
                    />
                  </Grid>
                  <Grid item xs={5}>
                    <YBInputField
                      name={`renamedKeyspaces.${ind}.renamedKeyspace`}
                      control={control}
                      fullWidth
                      className={classes.newKeyspaceName}
                    />
                  </Grid>
                </Grid>
              );
            })}
        </Grid>
      </div>
    </Box>
  );
});

export default RenameKeyspace;
