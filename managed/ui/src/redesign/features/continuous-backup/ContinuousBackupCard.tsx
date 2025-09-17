import { makeStyles, Typography } from '@material-ui/core';
import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import moment from 'moment';
import copy from 'copy-to-clipboard';

import { ContinuousBackup } from '../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ReactComponent as TrashIcon } from '../../assets/trashbin.svg';
import { ReactComponent as PenIcon } from '../../assets/pen.svg';
import { ReactComponent as CopyIcon } from '../../assets/copy.svg';
import { ReactComponent as ErrorIcon } from '../../assets/error-circle.svg';
import { YBButton } from '../../components';
import {
  ConfigureContinuousBackupModal,
  ConfigureContinuousBackupOperation
} from './ConfigureContinuousBackupModal';
import { formatDatetime } from '../../helpers/DateUtils';
import { DeleteContinuousBackupConfigModal } from './DeleteContinuousBackupConfigModal';

interface ContinuousBackupCardProps {
  continuousBackupConfig: ContinuousBackup;
}

const useStyles = makeStyles((theme) => ({
  card: {
    boxShadow: '0px 4px 10px 0px rgba(0, 0, 0, 0.05);',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.shape.borderRadius
  },
  cardHeader: {
    display: 'flex',
    gap: theme.spacing(3),
    alignItems: 'center',

    padding: `${theme.spacing(1)}px ${theme.spacing(3)}px`,

    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  cardBody: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(4),

    padding: `${theme.spacing(4)}px ${theme.spacing(3)}px`
  },
  cardActionsContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    marginLeft: 'auto'
  },
  icon: {
    marginRight: theme.spacing(0.5)
  },
  metadataContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  },
  metadataLabel: {
    color: theme.palette.grey[600],
    fontSize: '11.5px',
    fontWeight: 500,
    textTransform: 'uppercase'
  },
  metadataInformationContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center'
  },
  metadataInformation: {
    color: theme.palette.grey[900],
    fontSize: '13px',
    fontWeight: 400
  },
  copyIcon: {
    width: '24px',
    height: '24px',
    color: theme.palette.primary[600],

    '&:hover': {
      cursor: 'pointer'
    }
  },
  noRecentBackupBanner: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    width: '600px',
    padding: theme.spacing(1),
    marginTop: theme.spacing(2),

    background: theme.palette.error[100],
    borderRadius: theme.shape.borderRadius
  }
}));

const RECENT_BACKUP_THRESHOLD_HOURS = 24;
const TRANSLATION_KEY_PREFIX = 'continuousBackup.continuousBackupCard';

export const ContinuousBackupCard = ({ continuousBackupConfig }: ContinuousBackupCardProps) => {
  const [isConfigureContinuousBackupModalOpen, setIsConfigureContinuousBackupModalOpen] = useState(
    false
  );
  const [isDeleteContinuousBackupModalOpen, setIsDeleteContinuousBackupModalOpen] = useState(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const openConfigureContinuousBackupModal = () => setIsConfigureContinuousBackupModalOpen(true);
  const closeConfigureContinuousBackupModal = () => setIsConfigureContinuousBackupModalOpen(false);
  const openDeleteContinuousBackupModal = () => setIsDeleteContinuousBackupModalOpen(true);
  const closeDeleteContinuousBackupModal = () => setIsDeleteContinuousBackupModalOpen(false);

  const currentTime = moment();
  const lastBackupTime = continuousBackupConfig.info?.last_backup;
  const storageLocation = continuousBackupConfig.info?.storage_location;
  const handleStorageLocationCopy = () => {
    if (storageLocation) {
      copy(storageLocation);
    }
  };
  const shouldShowNoRecentBackupBanner =
    currentTime.diff(lastBackupTime, 'hours') > RECENT_BACKUP_THRESHOLD_HOURS;
  return (
    <div className={classes.card}>
      <div className={classes.cardHeader}>
        <Typography variant="h5">{t('title')}</Typography>
        <div className={classes.cardActionsContainer}>
          <YBButton variant="secondary" onClick={openConfigureContinuousBackupModal}>
            <PenIcon className={classes.icon} />
            <Typography variant="body2">{t('edit', { keyPrefix: 'common' })}</Typography>
          </YBButton>
          <YBButton variant="secondary" onClick={openDeleteContinuousBackupModal}>
            <TrashIcon className={classes.icon} />
            <Typography variant="body2">{t('button.remove')}</Typography>
          </YBButton>
        </div>
      </div>
      <div className={classes.cardBody}>
        <div className={classes.metadataContainer}>
          <Typography className={classes.metadataLabel}>{t('metadata.lastBackup')}</Typography>
          <Typography className={classes.metadataInformation}>
            {formatDatetime(lastBackupTime)}
          </Typography>

          {shouldShowNoRecentBackupBanner && (
            <div className={classes.noRecentBackupBanner}>
              <ErrorIcon />
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.noRecentBackupError`}
                  components={{ underline: <u /> }}
                />
              </Typography>
            </div>
          )}
        </div>
        <div className={classes.metadataContainer}>
          <Typography className={classes.metadataLabel}>{t('metadata.backupInterval')}</Typography>
          <Typography className={classes.metadataInformation}>
            {t('backupIntervalValue', {
              backupFrequencyMinutes: continuousBackupConfig.spec?.frequency
            })}
          </Typography>
        </div>
        <div className={classes.metadataContainer}>
          <Typography className={classes.metadataLabel}>{t('metadata.storageLocation')}</Typography>
          <div className={classes.metadataInformationContainer}>
            <Typography className={classes.metadataInformation}>{storageLocation}</Typography>
            {storageLocation && (
              <CopyIcon className={classes.copyIcon} onClick={handleStorageLocationCopy} />
            )}
          </div>
        </div>
      </div>
      <ConfigureContinuousBackupModal
        continuousBackupConfig={continuousBackupConfig}
        operation={ConfigureContinuousBackupOperation.EDIT}
        modalProps={{
          open: isConfigureContinuousBackupModalOpen,
          onClose: closeConfigureContinuousBackupModal
        }}
      />
      <DeleteContinuousBackupConfigModal
        continuousBackupConfigUuid={continuousBackupConfig.info?.uuid}
        modalProps={{
          open: isDeleteContinuousBackupModalOpen,
          onClose: closeDeleteContinuousBackupModal
        }}
      />
    </div>
  );
};
