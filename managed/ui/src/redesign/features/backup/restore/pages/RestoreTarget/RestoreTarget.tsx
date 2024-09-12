/*
 * Created on Wed Aug 21 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useEffect, useImperativeHandle } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { toast } from 'react-toastify';

import { Divider, makeStyles, Typography } from '@material-ui/core';
import SelectKMS from './SelectKMS';
import SelectUniverse from './SelectUniverse';
import RenameKeyspaceOption from './RenameKeyspaceOption';
import RestoreTablespacesOption from './RestoreTablespacesOption';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { getPreflightCheck } from '../../api/api';
import { isDefinedNotNull, isNonEmptyString } from '../../../../../../utils/ObjectUtils';
import { isDuplicateKeyspaceExistsinUniverse } from '../../../../../../components/backupv2/components/restore/RestoreUtils';
import { GetRestoreContext } from '../../RestoreUtils';
import { fetchTablesInUniverse } from '../../../../../../actions/xClusterReplication';
import { TableType } from '../../../../../helpers/dtos';
import ParallelThreadsConfig from './ParallelThreads';
import { Page, PageRef } from '../../models/RestoreContext';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px',
    display: 'flex',
    flexDirection: 'column',
    gap: '16px'
  },
  divider: {
    margin: '8px 0',
    background: theme.palette.ybacolors.ybBorderGray
  },
  title: {
    fontWeight: 700
  }
}));

const RestoreTarget = forwardRef<PageRef>((_, forwardRef) => {
  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.target'
  });

  const [
    { backupDetails },
    { setDisableSubmit, setisSubmitting, setSubmitLabel, moveToPage, setPreflightResponse }
  ] = GetRestoreContext();

  const { watch, getValues, setValue, handleSubmit } = useFormContext<RestoreFormModel>();

  const targetUniverse = watch('target.targetUniverse');
  const renameKeyspace = watch('target.renameKeyspace');
  const keyspaceToRestore = getValues().keyspacesToRestore;

  const { data: preflightRespData, isSuccess: isPreflightSuccess, isFetching } = useQuery(
    ['backup', 'preflight', targetUniverse?.value],
    () =>
      getPreflightCheck({
        backupUUID: keyspaceToRestore!.backupUUID,
        universeUUID: targetUniverse!.value,
        keyspaceTables: keyspaceToRestore!.keyspaceTables,
        restoreToPointInTimeMillis: keyspaceToRestore?.restoreToPointInTimeMillis
      }),
    {
      enabled: isNonEmptyString(targetUniverse?.value) && isDefinedNotNull(backupDetails),
      onSuccess(data) {
        setDisableSubmit(false);
        setPreflightResponse(data);
      },
      onError: () => {
        toast.error('Preflight check failed!.');
        setDisableSubmit(true);
      },
      onSettled: () => {
        setisSubmitting(false);
      }
    }
  );

  // we do duplicate check only for YSQL and when the preflight check is finished
  const enableVerifyDuplicateTable =
    isDefinedNotNull(targetUniverse?.value) &&
    backupDetails?.backupType === TableType.PGSQL_TABLE_TYPE &&
    isPreflightSuccess &&
    isDefinedNotNull(preflightRespData);

  useQuery(['tables', targetUniverse?.value], () => fetchTablesInUniverse(targetUniverse?.value), {
    enabled: enableVerifyDuplicateTable,
    onSuccess: (resp) => {
      if (isDuplicateKeyspaceExistsinUniverse(preflightRespData, resp.data)) {
        setValue('target.forceKeyspaceRename', true);
        setValue('target.renameKeyspace', true);
      }
    }
  });

  useEffect(() => {
    if (isFetching) {
      setDisableSubmit(true);
      setisSubmitting(true);
      setSubmitLabel(t('buttonLabels.verifying', { keyPrefix: 'backup' }));
    }
  }, [isFetching]);

  useEffect(() => {
    setValue('target.forceKeyspaceRename', false);
    setValue('target.renameKeyspace', false);
    setDisableSubmit(!targetUniverse?.value);
  }, [targetUniverse?.value]);

  // if the user chooses rename keyspaces, or "subset of tables",
  // change the modal's submit button to 'Next'
  useEffect(() => {
    if (isFetching) return;
    if (renameKeyspace) {
      setSubmitLabel(t('buttonLabels.next', { keyPrefix: 'backup' }));
    } else {
      setSubmitLabel(t('buttonLabels.restore', { keyPrefix: 'backup' }));
    }
  }, [renameKeyspace, setSubmitLabel, t, isFetching]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        handleSubmit(() => {
          if (renameKeyspace) {
            moveToPage(Page.RENAME_KEYSPACES);
          } else {
            moveToPage(Page.RESTORE_FINAL);
          }
        })();
      },
      onPrev: () => {
        moveToPage(Page.SOURCE);
      }
    }),
    [renameKeyspace]
  );

  return (
    <div className={classes.root}>
      <Typography className={classes.title} variant="body1">
        {t('selectUniverse')}
      </Typography>
      <SelectUniverse />
      <RenameKeyspaceOption />
      <RestoreTablespacesOption />
      <Divider className={classes.divider} />
      <Typography className={classes.title} variant="body1">
        {t('selectKMS')}
      </Typography>
      <SelectKMS />
      {backupDetails?.category === 'YB_BACKUP_SCRIPT' && (
        <section>
          <ParallelThreadsConfig />
        </section>
      )}
    </div>
  );
});

RestoreTarget.displayName = 'SelectKeyspaces';

export default RestoreTarget;
