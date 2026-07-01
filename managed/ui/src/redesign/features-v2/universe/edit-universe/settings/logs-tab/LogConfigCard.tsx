import { FC, ReactNode } from 'react';
import { Link, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton } from '@yugabyte-ui-library/core';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';

const TRANSLATION_KEY_PREFIX = 'editUniverse.logs';

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1.25),

    width: '100%',
    padding: theme.spacing(3),

    backgroundColor: theme.palette.ybacolors.grey005,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  content: {
    flex: 1,

    display: 'flex',
    alignItems: 'center',

    minWidth: 0
  },
  text: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    maxWidth: '658px'
  },
  titleRow: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  icon: {
    flexShrink: 0,

    display: 'flex',

    width: '20px',
    height: '20px'
  },
  title: {
    color: theme.palette.grey[900],
    fontSize: '13px',
    fontWeight: 600,
    lineHeight: '16px'
  },
  description: {
    color: theme.palette.grey[800],
    fontSize: '13px',
    fontWeight: 400,
    lineHeight: '16px'
  },
  learnMoreLink: {
    color: theme.palette.primary[600],
    fontSize: '13px',
    fontWeight: 400,
    lineHeight: '16px',
    textDecoration: 'underline',
    textDecorationStyle: 'solid',
    textUnderlinePosition: 'from-font'
  }
}));

export interface LogConfigCardProps {
  icon: ReactNode;
  title: string;
  description: string;
  learnMoreUrl: string;
  actionLabel: string;
  actionIcon?: ReactNode;
  actionVariant?: 'secondary' | 'ghost';
  onActionClick?: () => void;
  actionDisabled?: boolean;
  actionTestId: string;
}

export const LogConfigCard: FC<LogConfigCardProps> = ({
  icon,
  title,
  description,
  learnMoreUrl,
  actionLabel,
  actionIcon,
  actionVariant = 'secondary',
  onActionClick,
  actionDisabled = false,
  actionTestId
}) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  return (
    <div className={classes.root}>
      <div className={classes.content}>
        <div className={classes.text}>
          <div className={classes.titleRow}>
            <div className={classes.icon}>{icon}</div>
            <Typography className={classes.title}>{title}</Typography>
          </div>
          <Typography className={classes.description}>
            {description}{' '}
            <Link
              className={classes.learnMoreLink}
              href={learnMoreUrl}
              target="_blank"
              rel="noopener noreferrer"
            >
              {t('learnMore')}
            </Link>
          </Typography>
        </div>
      </div>
      <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
        <YBButton
          dataTestId={actionTestId}
          variant={actionVariant}
          startIcon={actionIcon}
          disabled={actionDisabled}
          onClick={onActionClick}
        >
          {actionLabel}
        </YBButton>
      </RbacValidator>
    </div>
  );
};
