import { FC, ReactNode } from 'react';
import { Link, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { mui, StatusType, YBButton, YBDropdown, YBSmartStatus } from '@yugabyte-ui-library/core';

import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';

import EditIcon from '@app/redesign/assets/approved/edit.svg';
import DropdownArrowIcon from '@app/redesign/assets/approved/triangle-arrow-down.svg';

const { MenuItem, Divider } = mui;

const TRANSLATION_KEY_PREFIX = 'editUniverse.logs';

const CONFIGURATION_MENU_KEYS = {
  query: {
    trigger: 'editQueryLogging',
    edit: 'editDatabaseQueryLogging',
    disable: 'disableDatabaseQueryLogging'
  },
  audit: {
    trigger: 'editAuditLogging',
    edit: 'editDatabaseAuditLogging',
    disable: 'disableDatabaseAuditLogging'
  }
} as const;

const useStyles = makeStyles((theme) => ({
  titleGroup: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    color: theme.palette.grey[900],
    fontSize: '13px',
    fontWeight: 600,
    lineHeight: '16px'
  },
  icon: {
    flexShrink: 0,

    display: 'flex',

    width: '20px',
    height: '20px'
  },
  unconfiguredCard: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: theme.spacing(2),

    width: '100%',
    padding: theme.spacing(3),

    backgroundColor: theme.palette.ybacolors.grey005,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  unconfiguredText: {
    flex: 1,

    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    minWidth: 0,
    maxWidth: '658px'
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
  },
  configuredCard: {
    display: 'flex',
    flexDirection: 'column',

    width: '100%',
    overflow: 'hidden',

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: theme.spacing(2),

    minHeight: '64px',
    padding: theme.spacing(1.25, 3)
  },
  statusRow: {
    display: 'flex',
    alignItems: 'center',

    padding: theme.spacing(1, 3, 3, 3)
  },
  statusItem: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  },
  statusLabel: {
    color: theme.palette.grey[600],
    fontSize: '11.5px',
    fontWeight: 500,
    lineHeight: '16px',
    textTransform: 'uppercase'
  },
  statusValue: {
    display: 'flex',
    alignItems: 'center',

    minHeight: '24px'
  }
}));

interface LogConfigCardCommonProps {
  icon: ReactNode;
  title: string;
  actionDisabled?: boolean;
  actionTestId: string;
}

interface LogConfigCardUnconfiguredProps extends LogConfigCardCommonProps {
  unconfigured: true;
  actionLabel: string;
  onActionClick?: () => void;
  description: string;
  learnMoreUrl: string;
}

export type LogConfigStatus = 'active' | 'configuring';

interface LogConfigCardConfiguredProps extends LogConfigCardCommonProps {
  unconfigured?: false;
  logStatus: LogConfigStatus;
  logType: 'query' | 'audit';
  onEditClick?: () => void;
  onDisableClick?: () => void;
}

export type LogConfigCardProps =
  | LogConfigCardUnconfiguredProps
  | LogConfigCardConfiguredProps;

export const LogConfigCard: FC<LogConfigCardProps> = (props) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const { icon, title, actionDisabled = false, actionTestId } = props;

  const titleGroup = (
    <Typography className={classes.titleGroup} component="div">
      <span className={classes.icon}>{icon}</span>
      {title}
    </Typography>
  );

  if (!props.unconfigured) {
    const menuKeys = CONFIGURATION_MENU_KEYS[props.logType];
    const isConfiguring = props.logStatus === 'configuring';
    const statusType = isConfiguring ? StatusType.IN_PROGRESS : StatusType.SUCCESS;
    const statusLabel = isConfiguring ? t('configuring') : t('active');

    return (
      <div className={classes.configuredCard}>
        <div className={classes.header}>
          {titleGroup}
          <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
            <YBDropdown
              growDirection="left"
              dataTestId={actionTestId}
              disabled={actionDisabled}
              slotProps={{
                paper: {
                  sx: {
                    minWidth: 220,
                    width: 'max-content',
                    py: 1,
                    border: `1px solid ${theme.palette.grey[200]}`
                  }
                }
              }}
              origin={
                <YBButton
                  variant="ghost"
                  dataTestId={`${actionTestId}-Trigger`}
                  startIcon={<EditIcon width={20} height={20} />}
                  endIcon={<DropdownArrowIcon width={16} height={16} />}
                  disabled={actionDisabled}
                >
                  {t(menuKeys.trigger)}
                </YBButton>
              }
            >
              <MenuItem
                data-testid={`${actionTestId}-Edit`}
                onClick={props.onEditClick}
                disabled={actionDisabled}
              >
                {t(menuKeys.edit)}
              </MenuItem>
              <Divider sx={{ borderColor: theme.palette.grey[200], my: 0.5 }} />
              <MenuItem
                data-testid={`${actionTestId}-Disable`}
                onClick={props.onDisableClick}
                disabled={actionDisabled || !props.onDisableClick}
              >
                {t(menuKeys.disable)}
              </MenuItem>
            </YBDropdown>
          </RbacValidator>
        </div>
        <div className={classes.statusRow}>
          <div className={classes.statusItem}>
            <Typography className={classes.statusLabel}>{t('logStatus')}</Typography>
            <div className={classes.statusValue}>
              <YBSmartStatus type={statusType} label={statusLabel} />
            </div>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className={classes.unconfiguredCard}>
      <div className={classes.unconfiguredText}>
        {titleGroup}
        <Typography className={classes.description}>
          {props.description}{' '}
          <Link
            className={classes.learnMoreLink}
            href={props.learnMoreUrl}
            target="_blank"
            rel="noopener noreferrer"
          >
            {t('learnMore')}
          </Link>
        </Typography>
      </div>
      <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
        <YBButton
          dataTestId={actionTestId}
          variant="secondary"
          disabled={actionDisabled}
          onClick={props.onActionClick}
        >
          {props.actionLabel}
        </YBButton>
      </RbacValidator>
    </div>
  );
};
