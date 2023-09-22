/*
 * Created on Wed Sep 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useContext } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { isEmpty, keys } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { AlertVariant, YBAlert, YBModal } from '../../../../../../redesign/components';
import { IGeneralSettings } from './GeneralSettings';
import { RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { Typography, makeStyles } from '@material-ui/core';

type TablespaceUnsupportedDetailsProps = {
  visible: boolean;
  onHide: () => void;
};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(2),
    color: theme.palette.ybacolors.ybDarkGray
  },
  subTitle: {
    marginTop: theme.spacing(4),
    marginBottom: theme.spacing(1)
  },
  errMsgPane: {
    marginTop: theme.spacing(1),
    marginBottom: theme.spacing(2),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.common.white,
    padding: `12px 16px`,
    borderRadius: '8px'
  },
  alert: {
    alignItems: 'flex-start',
    '& span': {
      padding: '4px 0'
    }
  },
  heading: {
    marginBottom: theme.spacing(3)
  },
  tablespaceName: {
    textTransform: 'uppercase',
    color: '#67666C'
  },
  tablespaceItem: {
    marginTop: '4px'
  },
  link: {
    color: theme.palette.ybacolors.ybDarkGray,
    textDecoration: 'underline'
  }
}));

export const TablespaceUnsupportedDetails: FC<TablespaceUnsupportedDetailsProps> = ({
  visible,
  onHide
}) => {
  const [
    {
      formData: { preflightResponse }
    },
    ,
    { hideModal }
  ]: RestoreContextMethods = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;
  const { setValue } = useFormContext<IGeneralSettings>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'newRestoreModal.tablespaces.unSupportedModal'
  });

  const classes = useStyles();

  if (!preflightResponse || !visible) return null;

  const storageLocationsKeys = keys(preflightResponse.perLocationBackupInfoMap);

  const [conflictingTablespaces, unsupportedTablespaces] = storageLocationsKeys.reduce(
    (prevValues, currentValue) => {

      // In restore entire backup, if there are multiple configs, just show the first one
      if (prevValues[0].length !== 0 || prevValues[1].length !== 0) return prevValues;

      if (
        !isEmpty(
          preflightResponse.perLocationBackupInfoMap[currentValue].tablespaceResponse
            .conflictingTablespaces
        )
      ) {
        prevValues[0].push(
          ...(preflightResponse.perLocationBackupInfoMap[currentValue].tablespaceResponse
            .conflictingTablespaces as string[])
        );
      }
      if (
        !isEmpty(
          preflightResponse.perLocationBackupInfoMap[currentValue].tablespaceResponse
            .unsupportedTablespaces
        )
      ) {
        prevValues[1].push(
          ...(preflightResponse.perLocationBackupInfoMap[currentValue].tablespaceResponse
            .unsupportedTablespaces as string[])
        );
      }
      return prevValues;
    },
    [[], []] as [string[], string[]]
  );

  return (
    <YBModal
      open={visible}
      size="md"
      style={{
        position: 'fixed',
        zIndex: 1000000
      }}
      title={t('title')}
      dialogContentProps={{
        dividers: true,
        className: classes.root
      }}
      overrideHeight={'690px'}
      overrideWidth={'800px'}
      submitLabel={t('submitLabel')}
      cancelLabel={t('cancelLabel')}
      onClose={() => {
        setValue('useTablespaces', false);
        onHide();
      }}
      onSubmit={() => {
        setValue('useTablespaces', false);
        onHide();
      }}
      buttonProps={{
        secondary: {
          onClick: hideModal
        }
      }}
    >
      <YBAlert
        open={true}
        text={
          <Trans
            i18nKey="newRestoreModal.tablespaces.unSupportedModal.warningText"
            components={{ b: <b />, a: <a className={classes.link} href="https://docs.yugabyte.com/preview/explore/ysql-language-features/going-beyond-sql/tablespaces" rel="noreferrer" target='_blank'></a> }}
          />
        }
        variant={AlertVariant.Warning}
        className={classes.alert}
      />
      <Typography variant="body1" className={classes.subTitle}>
        {t('subTitle')}
      </Typography>
      <Typography variant="body2">
        <Trans
          i18nKey="newRestoreModal.tablespaces.unSupportedModal.logs"
          components={{ a: <a className={classes.link} href={`/logs?queryRegex=${preflightResponse.loggingID}`} rel="noreferrer" target='_blank'></a> }}
        />
      </Typography>
      {conflictingTablespaces.length !== 0 && (
        <div className={classes.errMsgPane}>
          <Typography variant="body2" className={classes.heading}>
            {t('topologyMismatch')}
          </Typography>
          <Typography variant="subtitle1" className={classes.tablespaceName}>
            {t('tablespaceName')}
          </Typography>
          {conflictingTablespaces.map((t) => (
            <div className={classes.tablespaceItem} key={t}>{t}</div>
          ))}
        </div>
      )}
      {unsupportedTablespaces.length !== 0 && (
        <div className={classes.errMsgPane}>
          <Typography variant="body2" className={classes.heading}>
            {t('nameAlreadyExists')}
          </Typography>
          <Typography variant="subtitle1" className={classes.tablespaceName}>
            {t('tablespaceName')}
          </Typography>
          {unsupportedTablespaces.map((t) => (
            <div className={classes.tablespaceItem} key={t}>{t}</div>
          ))}
        </div>
      )}
    </YBModal>
  );
};
