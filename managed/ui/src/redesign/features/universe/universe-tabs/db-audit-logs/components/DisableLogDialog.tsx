import { FC } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { YBModal } from '../../../../../components';
//RBAC
import { hasNecessaryPerm } from '../../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../../../rbac/common/validator/ValidatorUtils';
//styles
import { auditLogStyles } from '../utils/AuditLogStyles';

interface DisableLogDialogProps {
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
  universeName: string;
  universeUUID: string;
}

export const DisableLogDialog: FC<DisableLogDialogProps> = ({
  open,
  onClose,
  onSubmit,
  universeName,
  universeUUID
}) => {
  const classes = auditLogStyles();
  const { t } = useTranslation();

  const canUpdateAuditLog = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.ENABLE_AUDITLOG_CONFIG
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      overrideHeight={'370px'}
      overrideWidth={'680px'}
      size="sm"
      title={t('dbAuitLog.disableLogTitle')}
      submitLabel={t('dbAuitLog.disableLogSubmitLabel')}
      cancelLabel={t('common.cancel')}
      onSubmit={onSubmit}
      submitTestId="DisableLogDialog-Submit"
      cancelTestId="DisableLogDialog-Cancel"
      buttonProps={{
        primary: {
          disabled: !canUpdateAuditLog
        }
      }}
      submitButtonTooltip={!canUpdateAuditLog ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <Box className={classes.disableLogModalConatiner}>
        <Typography className={classes.exportInfoText}>
          {t('dbAuitLog.disableLogWarning')}
        </Typography>
        <Box className={classes.exportInfo}>
          <Typography className={classes.exportInfoText}>
            <Trans i18nKey={'dbAuitLog.disableLogMessage'} values={{ universeName }} />
          </Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
