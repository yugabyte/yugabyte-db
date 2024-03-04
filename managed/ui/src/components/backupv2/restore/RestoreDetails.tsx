/*
 * Created on Wed Aug 23 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useContext } from 'react';
import copy from 'copy-to-clipboard';
import { useQuery } from 'react-query';
import { find, isEmpty } from 'lodash';
import { Link } from 'react-router';
import { useTranslation } from 'react-i18next';

import { Typography, makeStyles } from '@material-ui/core';
import { toast } from 'react-toastify';
import { YBButton } from '../../../redesign/components';
import { formatBytes } from '../../xcluster/ReplicationUtils';
import { ENTITY_NOT_AVAILABLE, calculateDuration } from '../common/BackupUtils';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { api } from '../../../redesign/helpers/api';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { IRestore, Restore_States } from '../common/IRestore';
import { RestoreContextMethods, RestoreDetailsContext } from './RestoreContext';
import { TableType } from '../../../redesign/helpers/dtos';
import ArrowRight from '../components/restore/icons/RightArrowCircled.svg';
import { useToggle } from 'react-use';
import clsx from 'clsx';

const useStyles = makeStyles((theme) => ({
  restoreDetailsPanel: {
    borderRadius: '2px',
    boxShadow: `0 0.12em 2px rgba(35, 35, 41, 0.05), 0 0.5em 10px rgba(35, 35, 41, 0.07)`,
    background: theme.palette.ybacolors.backgroundGrayLightest,
    height: '100%',
    width: '870px',
    position: 'fixed',
    zIndex: 9999,
    top: 0,
    right: 0,
    borderLeft: `1px solid #f7f7f7`,
    maxHeight: '100%'
  },
  sidePanelHeader: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    hieght: '68px',
    borderBottom: `1px solid #dedede`,
    padding: `${theme.spacing(3)}px ${theme.spacing(4)}px`
  },
  sidePanelTitle: {
    fontSize: '15px',
    fontWeight: 700,
    color: theme.palette.common.black
  },
  closeIcon: {
    cursor: 'pointer',
    '& i': {
      fontSize: '18px'
    }
  },
  sidePanelContent: {
    padding: theme.spacing(3.5)
  },
  restoreDetails: {
    borderRadius: theme.spacing(0.5),
    border: '1px solid #E3E3E5',
    background: theme.palette.ybacolors.backgroundGrayLight,
    height: '240px'
  },
  universeDetails: {
    padding: '20px',
    paddingTop: '15px',
    display: 'flex',
    gap: '36px',
    height: '80px'
  },
  arrowIcon: {
    width: '28px',
    height: '28px'
  },
  copyUUID: {
    borderRadius: theme.spacing(0.5),
    border: `1px solid #C8C8C8`,
    background: theme.palette.common.white,
    width: '60px',
    padding: '3px',
    marginLeft: theme.spacing(1),
    cursor: 'pointer'
  },
  header: {
    marginTop: theme.spacing(1)
  },
  divider: {
    height: '1px',
    background: '#E3E3E5',
    width: '100%'
  },
  restDetails: {
    padding: '20px',
    display: 'flex',
    columnGap: '72px',
    flexWrap: 'wrap',
    rowGap: '24px'
  },
  link: {
    color: theme.palette.orange[500],
    textDecoration: 'underline'
  }
}));

export const RestoreDetails = () => {
  const classes = useStyles();
  const [{ selectedRestore }, { setSelectedRestore }] = (useContext(
    RestoreDetailsContext
  ) as unknown) as RestoreContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'restore.restoreDetails'
  });

  const { data: universesList } = useQuery(['universes'], () => api.fetchUniverseList(), {
    refetchOnMount: false
  });

  if (!selectedRestore) return null;

  return (
    <div className={classes.restoreDetailsPanel}>
      <div className={classes.sidePanelHeader}>
        <div className={classes.sidePanelTitle}>{t('title')}</div>
        <span
          className={classes.closeIcon}
          onClick={() => {
            setSelectedRestore(null);
          }}
        >
          <i className="fa fa-close" />
        </span>
      </div>
      <div className={classes.sidePanelContent}>
        <div className={classes.restoreDetails}>
          <div className={classes.universeDetails}>
            <div>
              <Typography variant="body1" component="span">
                {t('sourceUniverse')}
              </Typography>
              <CopyUUID uuid={selectedRestore.sourceUniverseUUID} />
              {find(universesList, { universeUUID: selectedRestore.sourceUniverseUUID }) ? (
                <Link
                  target="_blank"
                  to={`/universes/${selectedRestore.sourceUniverseUUID}`}
                  className={classes.link}
                  onClick={(e) => {
                    e.stopPropagation();
                  }}
                >
                  <div className={classes.header}>
                    {selectedRestore.sourceUniverseName
                      ? selectedRestore.sourceUniverseName
                      : ENTITY_NOT_AVAILABLE}
                  </div>
                </Link>
              ) : (
                <div className={classes.header}>
                  {selectedRestore.sourceUniverseName
                    ? selectedRestore.sourceUniverseName
                    : ENTITY_NOT_AVAILABLE}
                </div>
              )}
            </div>
            <img className={classes.arrowIcon} src={ArrowRight} alt="arrowRight" />
            <div>
              <Typography variant="body1" component="span">
                {t('targetUniverse')}
              </Typography>
              <CopyUUID uuid={selectedRestore.universeUUID} />
              {find(universesList, { universeUUID: selectedRestore.universeUUID }) ? (
                <Link
                  target="_blank"
                  to={`/universes/${selectedRestore.universeUUID}`}
                  className={classes.link}
                  onClick={(e) => {
                    e.stopPropagation();
                  }}
                >
                  <div className={classes.header}>
                    {selectedRestore.targetUniverseName
                      ? selectedRestore.targetUniverseName
                      : ENTITY_NOT_AVAILABLE}
                  </div>
                </Link>
              ) : (
                <div className={classes.header}>
                  {selectedRestore.targetUniverseName
                    ? selectedRestore.targetUniverseName
                    : ENTITY_NOT_AVAILABLE}
                </div>
              )}
            </div>
          </div>
          <div className={classes.divider} />
          <div className={classes.restDetails}>
            <div>
              <Typography variant="body1" component="span">
                {t('dataRestored')}
              </Typography>
              <div>{formatBytes(selectedRestore.restoreSizeInBytes)}</div>
            </div>
            <div>
              <Typography variant="body1" component="span">
                {t('timeToRestore')}
              </Typography>
              <div>{calculateDuration(selectedRestore.createTime, selectedRestore.updateTime)}</div>
            </div>
            <div>
              <Typography variant="body1" component="span">
                {t('tableType')}
              </Typography>
              <div>
                {selectedRestore.backupType
                  ? selectedRestore?.backupType === TableType.PGSQL_TABLE_TYPE
                    ? 'YSQL'
                    : 'YCQL'
                  : '-'}
              </div>
            </div>
            <div>
              <Typography variant="body1" component="span">
                {t('backupDate')}
              </Typography>
              <div>
                {selectedRestore?.backupCreatedOnDate
                  ? ybFormatDate(selectedRestore.backupCreatedOnDate)
                  : '-'}
              </div>
            </div>
            <div>
              <Typography variant="body1" component="span">
                {t('restoreStatus')}
              </Typography>
              <div>
                <StatusBadge
                  statusType={(selectedRestore.state as unknown) as Badge_Types}
                  customLabel={
                    selectedRestore.state === Restore_States.FAILED ? t('restoreFailed') : undefined
                  }
                />
              </div>
            </div>
            <div>
              <Typography variant="body1" component="span">
                {t('startTime')}
              </Typography>
              <div>{ybFormatDate(selectedRestore.createTime)}</div>
            </div>
            <div>
              <Typography variant="body1" component="span">
                {t('endTime')}
              </Typography>
              <div>{ybFormatDate(selectedRestore.updateTime)}</div>
            </div>
          </div>
        </div>
        <RestoreDBDetails restoreDetails={selectedRestore} />
      </div>
    </div>
  );
};

const CopyUUID = ({ uuid }: { uuid: string }) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'restore.restoreDetails'
  });

  if (!uuid) return null;
  return (
    <span
      className={classes.copyUUID}
      onClick={() => {
        copy(uuid);
        toast.success(t('copied'));
      }}
    >
      <Typography variant="subtitle1" component="span">
        {t('copyUUID')}
      </Typography>
    </span>
  );
};

const RestoreDBDetailsStyles = makeStyles((theme) => ({
  root: {
    marginTop: '30px',
    padding: '20px'
  },
  title: {
    marginBottom: '12px'
  },
  databaseItem: {
    height: '46px',
    width: '100%',
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    borderTop: '1px solid rgba(0,0,0,0.1)',
    '&:last-child': {
      borderBottom: '1px solid rgba(0,0,0,0.1)'
    }
  },
  clickable: {
    cursor: 'pointer'
  },
  copyButton: {
    padding: '5px 10px',
    opacity: 0.9
  },
  buttonLabel: {
    color: theme.palette.ybacolors.ybDarkGray,
    fontSize: '12px',
    fontWeight: 500
  },
  tableNameList: {
    marginLeft: '75px',
    marginTop: '15px'
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-start',
    '& i': {
      fontSize: '18px',
      marginRight: '15px'
    }
  }
}));
type RestoreDBDetailsProps = {
  restoreDetails: IRestore;
};
const RestoreDBDetails: FC<RestoreDBDetailsProps> = ({ restoreDetails }) => {
  const classes = RestoreDBDetailsStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'restore.restoreDetails'
  });

  if (restoreDetails.restoreKeyspaceList.length === 0) return null;

  return (
    <div className={classes.root}>
      <Typography variant="body1" className={classes.title}>
        {t('database')}
      </Typography>
      {restoreDetails.restoreKeyspaceList.map((restore) => {
        return <DbItem restore={restore} restoreDetails={restoreDetails} />;
      })}
    </div>
  );
};

const COLLAPSED_ICON = <i className="fa fa-caret-right expand-keyspace-icon" />;
const EXPANDED_ICON = <i className="fa fa-caret-down expand-keyspace-icon" />;

const DbItem = ({
  restore,
  restoreDetails
}: {
  restore: IRestore['restoreKeyspaceList'][number];
  restoreDetails: IRestore;
}) => {
  const classes = RestoreDBDetailsStyles();

  const [expanded, toggleExpanded] = useToggle(false);

  const { t } = useTranslation('translation', {
    keyPrefix: 'restore.restoreDetails'
  });
  const hasTableList =
    restoreDetails.backupType === TableType.YQL_TABLE_TYPE && !isEmpty(restore.tableNameList);
  return (
    <>
      <div
        className={clsx(classes.databaseItem, hasTableList && classes.clickable)}
        onClick={() => {
          toggleExpanded(!expanded);
        }}
      >
        <span className={classes.header}>
          {hasTableList ? (expanded ? EXPANDED_ICON : COLLAPSED_ICON) : null}
          {restore.targetKeyspace}
        </span>
        <YBButton
          className={classes.copyButton}
          variant="secondary"
          onClick={(e) => {
            e.stopPropagation();
            copy(restore.storageLocation);
            toast.success(t('copied'));
          }}
        >
          <span className={classes.buttonLabel}>{t('copyBackupLocation')}</span>
        </YBButton>
      </div>
      {expanded && hasTableList && (
        <div className={classes.tableNameList}>
          <Typography variant="body1" className={classes.title}>
            {t('tables')}
          </Typography>
          {restore?.tableNameList?.map((t) => (
            <div key={t} className={classes.databaseItem}>
              {t}
            </div>
          ))}
        </div>
      )}
    </>
  );
};
