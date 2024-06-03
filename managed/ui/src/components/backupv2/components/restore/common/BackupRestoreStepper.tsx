/*
 * Created on Tue Jun 27 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useContext } from 'react';
import clsx from 'clsx';
import { size } from 'lodash';
import { makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { Page, RestoreContextMethods, RestoreFormContext } from '../RestoreContext';
import { TableType } from '../../../../../redesign/helpers/dtos';
import LeftArrow from '../icons/LeftArrow.svg';
import Check from '../icons/Check.svg';

type BackupRestoreStepperProps = {
  className?: any;
};

type IPages = Partial<
  {
    [key in Page]: string;
  }
>;

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    gap: theme.spacing(2)
  },
  step: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2),
    fontSize: '11.5px',
    fontWeight: 500,
    textTransform: 'uppercase',
    '& svg,img': {
      width: theme.spacing(3),
      height: theme.spacing(3)
    }
  },
  stepCount: {
    background: theme.palette.grey[300],
    color: theme.palette.common.white,
    width: theme.spacing(4),
    height: theme.spacing(4),
    borderRadius: theme.spacing(4),
    fontSize: '15px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center'
  },
  stepActive: {
    background: theme.palette.primary[600]
  },
  stepCompleted: {
    background: theme.palette.success[500]
  }
}));

export const BackupRestoreStepper: FC<BackupRestoreStepperProps> = ({ className }) => {
  const [
    {
      backupDetails,
      formData: { generalSettings },
      formProps
    }
  ]: RestoreContextMethods = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const classes = useStyles();

  const { t } = useTranslation();

  // only show stepper, if 'rename keyspace' and 'table selection' is selected
  if (
    backupDetails?.backupType === TableType.PGSQL_TABLE_TYPE ||
    !generalSettings?.renameKeyspace ||
    generalSettings.tableSelectionType !== 'SUBSET_OF_TABLES'
  ) {
    return null;
  }

  const pages: IPages = {
    GENERAL_SETTINGS: t('newRestoreModal.stepper.generalSettings'),
    RENAME_KEYSPACES: t('newRestoreModal.stepper.renameKeyspaces'),
    SELECT_TABLES: t('newRestoreModal.stepper.selectTables')
  };

  const getStepIndicationComponent = (pageIndex: number) => {
    const currentPageIndex = Object.keys(pages).indexOf(formProps.currentPage);
    if (pageIndex < currentPageIndex) {
      // if page is completed, show check mark
      return (
        <div className={clsx(classes.stepCount, classes.stepCompleted)}>
          <img alt="completed" src={Check} />
        </div>
      );
    }
    return (
      // show count
      <div
        className={clsx(classes.stepCount, pageIndex === currentPageIndex && classes.stepActive)}
      >
        {pageIndex + 1}
      </div>
    );
  };

  return (
    <div className={clsx(classes.root, className)}>
      {Object.keys(pages).map((p, index) => (
        <div className={classes.step} key={p}>
          {getStepIndicationComponent(index)}
          {pages[p]}
          {index !== size(pages) - 1 && <img alt="next" src={LeftArrow} />}
        </div>
      ))}
    </div>
  );
};
