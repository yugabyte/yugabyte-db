import { FC } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { YBModal } from '../../../../../components';
//styles
import { auditLogStyles } from '../utils/AuditLogStyles';

interface DisableLogDialogProps {
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
  universeName: string;
}

export const DisableLogDialog: FC<DisableLogDialogProps> = ({
  open,
  onClose,
  onSubmit,
  universeName
}) => {
  const classes = auditLogStyles();
  const { t } = useTranslation();
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
