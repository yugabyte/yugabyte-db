import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';

import ScheduleIcon from '../../assets/schedule.svg';
import DocumentationIcon from '../../assets/documentation.svg';
import TipIcon from '../../assets/tip.svg';

import { YBButton } from '../../components';
import { ApiPermissionMap } from '../rbac/ApiAndUserPermMapping';
import { RbacValidator } from '../rbac/common/RbacApiPermValidator';

interface EnableContinuousBackupPromptProps {
  isDisabled: boolean;
  onEnableContinuousBackupClick: () => void;

  className?: string;
}

const useStyles = makeStyles((theme) => ({
  promptContainer: {
    display: 'flex',
    alignItems: 'center',
    flexDirection: 'column',

    height: '360px',
    padding: `${theme.spacing(6)}px ${theme.spacing(2)}px ${theme.spacing(3)}px`,

    border: `1px dashed ${theme.palette.ybacolors.ybBorderGrayDark}`,
    borderRadius: '8px',
    background: theme.palette.ybacolors.backgroundGrayRegular
  },
  promptPrimaryText: {
    maxWidth: '550px',
    textAlign: 'center'
  },
  inlineIcon: {
    width: '16px',
    height: '16px'
  },
  tipIcon: {
    // The tip icon is placed slightly higher than center align with text.
    marginBottom: '3px'
  },
  learnMoreLink: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    color: theme.palette.ybacolors.accent_2_1,
    cursor: 'pointer'
  },
  backupUniverseTip: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    marginTop: 'auto'
  }
}));

const TRANSLATION_KEY_PREFIX = 'continuousBackup.enableContinuousBackupPrompt';

export const EnableContinuousBackupPrompt = ({
  className,
  isDisabled,
  onEnableContinuousBackupClick
}: EnableContinuousBackupPromptProps) => {
  const theme = useTheme();
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  return (
    <div className={clsx(classes.promptContainer, className)}>
      <Box display="flex" flexDirection="column" alignItems="center" gridGap={theme.spacing(3)}>
        <ScheduleIcon />
        <Typography className={classes.promptPrimaryText} variant="body2">
          {t('featureDescription')}
        </Typography>
        <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_CONTINUOUS_YBA_BACKUP} isControl>
          <YBButton
            style={{ minWidth: '200px' }}
            variant="primary"
            onClick={onEnableContinuousBackupClick}
            disabled={isDisabled}
            data-testid={`EnableContinuousBackupPrompt-EnableButton`}
          >
            {t('actionButton')}
          </YBButton>
        </RbacValidator>
        <Typography variant="body2" className={classes.learnMoreLink}>
          <DocumentationIcon className={classes.inlineIcon} />
          {/* TODO: Docs link. */}
          <a href={'https://docs.yugabyte.com'} target="_blank" rel="noopener noreferrer">
            {t('learnMore')}
          </a>
        </Typography>
      </Box>
      <Typography className={classes.backupUniverseTip} variant="body2">
        <TipIcon className={clsx(classes.inlineIcon, classes.tipIcon)} />
        {t('backupUniverseTip')}
      </Typography>
    </div>
  );
};
