import { useState } from 'react';
import { makeStyles } from '@material-ui/core/styles';
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { Link } from 'react-router';

import { YBButton } from '../../../redesign/components/YBButton/YBButton';
import BriefcaseIcon from '@app/redesign/assets/briefcase.svg';
import BulbIcon from '@app/redesign/assets/bulb2.svg';
import { RestoreYbaBackupModal } from '@app/redesign/features/continuous-backup/RestoreYbaBackupModal';
import {
  hasNecessaryPerm,
  RbacValidator
} from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';

const useStyles = makeStyles((theme) => ({
  container: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: theme.spacing(4),

    padding: theme.spacing(4)
  },
  title: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  welcomeToText: {
    fontSize: 18,
    fontWeight: 500
  },
  ybaTitle: {
    fontSize: 32,
    fontWeight: 600,
    color: theme.palette.primary[700],
    marginBottom: theme.spacing(2)
  },
  cardsContainer: {
    display: 'flex',
    gap: theme.spacing(5),
    justifyContent: 'center',
    flexWrap: 'wrap'
  },
  card: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: theme.spacing(3),

    width: 556,
    height: 398,
    padding: theme.spacing(12, 6, 6),

    border: `2px dashed ${theme.palette.grey[300]}`,
    borderRadius: theme.spacing(1),
    backgroundColor: theme.palette.background.paper
  },
  icon: {
    color: theme.palette.grey[600],
    marginBottom: theme.spacing(1)
  },
  cardText: {
    width: 275,

    fontSize: 15,
    fontWeight: 500,
    color: theme.palette.text.primary,
    textAlign: 'center'
  }
}));

const TRANSLATION_KEY_PREFIX = 'dashboard.onboardingPanel';

export const OnboardingPanel = () => {
  const [isRestoreYbaBackupModalOpen, setIsRestoreYbaBackupModalOpen] = useState(false);
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const openRestoreYbaBackupModal = () => setIsRestoreYbaBackupModalOpen(true);
  const closeRestoreYbaBackupModal = () => setIsRestoreYbaBackupModalOpen(false);

  return (
    <div className={classes.container}>
      <div className={classes.title}>
        <Typography className={classes.welcomeToText}>{t('welcomeTo')}</Typography>
        <Typography className={classes.ybaTitle}>{t('yugabyteDbAnywhere')}</Typography>
      </div>

      <div className={classes.cardsContainer}>
        <div className={classes.card}>
          <BulbIcon width={62} height={62} />
          <Typography className={classes.cardText}>{t('newUserCard.title')}</Typography>
          <Link to="/config">
            <YBButton variant="primary">{t('newUserCard.buttonText')}</YBButton>
          </Link>
        </div>

        <div className={classes.card}>
          <BriefcaseIcon width={62} height={62} />
          <Typography className={classes.cardText}>{t('restoreBackupCard.title')}</Typography>
          <RbacValidator
            customValidateFunction={() =>
              hasNecessaryPerm(ApiPermissionMap.RESTORE_CONTINUOUS_YBA_BACKUP) ||
              hasNecessaryPerm(ApiPermissionMap.RESTORE_ISOLATED_YBA_BACKUP)
            }
            isControl
          >
            <YBButton variant="primary" onClick={openRestoreYbaBackupModal}>
              {t('restoreBackupCard.buttonText')}
            </YBButton>
          </RbacValidator>
        </div>
      </div>

      {isRestoreYbaBackupModalOpen && (
        <RestoreYbaBackupModal
          modalProps={{ open: isRestoreYbaBackupModalOpen, onClose: closeRestoreYbaBackupModal }}
        />
      )}
    </div>
  );
};
