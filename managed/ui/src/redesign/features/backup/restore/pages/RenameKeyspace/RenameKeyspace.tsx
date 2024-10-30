/*
 * Created on Mon Aug 26 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useImperativeHandle, useState } from 'react';
import { useMount } from 'react-use';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { differenceWith, intersectionWith, uniq, values } from 'lodash';

import { Box, FormHelperText, Grid, makeStyles, Typography } from '@material-ui/core';
import { YBSearchInput } from '../../../../../../components/common/forms/fields/YBSearchInput';
import { YBInput, YBInputField } from '../../../../../components';
import {
  YBErrorIndicator,
  YBLoadingCircleIcon
} from '../../../../../../components/common/indicators';

import { fetchTablesInUniverse } from '../../../../../../actions/xClusterReplication';
import { GetRestoreContext } from '../../RestoreUtils';

import { ITable } from '../../../../../../components/backupv2';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { Page, PageRef } from '../../models/RestoreContext';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px'
  },
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
    gap: '20px'
  },
  currentKeyspaceName: {
    width: '340px',
    '& .MuiInput-root': {
      background: '#F7F7F7'
    }
  },
  newKeyspaceName: {
    width: '340px'
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
  }
}));

const RenameKeyspace = forwardRef<PageRef>((_, forwardRef) => {
  const classes = useStyles();
  const { t } = useTranslation('translation');
  const {
    control,
    watch,
    handleSubmit,
    formState: { errors }
  } = useFormContext<RestoreFormModel>();
  const [
    { backupDetails },
    { moveToPage, setSubmitLabel, setKeyspacesInTargetUniverse }
  ] = GetRestoreContext();
  const [searchText, setSearchText] = useState('');

  const forceKeyspaceRename = watch('target.forceKeyspaceRename');
  const keyspacesToRestore = watch('keyspacesToRestore');
  const universe = watch('target.targetUniverse');

  const { data: tablesInTargetUniverse, isLoading: isTableListLoading, isError } = useQuery(
    [universe?.value, 'tables'],
    () => fetchTablesInUniverse(universe!.value!),
    {
      select: (data) => data.data,
      onSuccess(data) {
        setKeyspacesInTargetUniverse(uniq(data.map((k: ITable) => k.keySpace)));
      }
    }
  );

  const keyspacesAvailableInTargetUniverse: string[] =
    tablesInTargetUniverse
      ?.filter((t: ITable) => t.tableType === (backupDetails?.backupType as any))
      .map((t: ITable) => t.keySpace) ?? [];

  // move the keyspaces which are duplicate to the top.
  const sortByTableDuplicate = [
    ...intersectionWith(
      values(keyspacesToRestore?.keyspaceTables),
      keyspacesAvailableInTargetUniverse,
      (selectedKeyspaces, keyspace) => {
        return keyspace === selectedKeyspaces.keyspace;
      }
    ),
    ...differenceWith(
      values(keyspacesToRestore?.keyspaceTables),
      keyspacesAvailableInTargetUniverse,
      (selectedKeyspaces, keyspace) => {
        return keyspace === selectedKeyspaces.keyspace;
      }
    )
  ];

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        handleSubmit(() => {
          moveToPage(Page.RESTORE_FINAL);
        })();
      },
      onPrev: () => {
        moveToPage(Page.TARGET);
      }
    }),
    []
  );

  useMount(() => {
    setSubmitLabel(t('buttonLabels.restore', { keyPrefix: 'backup' }));
  });

  if (isTableListLoading) return <YBLoadingCircleIcon />;

  if (isError) return <YBErrorIndicator />;

  return (
    <Box className={classes.root}>
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
          {sortByTableDuplicate.map((e: any, ind: number) => {
            const origKeyspaceName = e.keyspace;
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
                      t('backup.restore.validationErrMsg.keyspacesAlreadyExists')
                    }
                    className={classes.currentKeyspaceName}
                  />
                </Grid>
                <Grid item xs={5}>
                  <YBInputField
                    name={`renamedKeyspace.${ind}.renamedKeyspace`}
                    control={control}
                    fullWidth
                    className={classes.newKeyspaceName}
                  />
                  {
                    <FormHelperText error>
                      {(errors as any)?.[`renamedKeyspace[${ind}].renamedKeyspace`]?.message}
                    </FormHelperText>
                  }
                </Grid>
              </Grid>
            );
          })}
        </Grid>
      </div>
    </Box>
  );
});

RenameKeyspace.displayName = 'RenameKeyspace';
export default RenameKeyspace;
