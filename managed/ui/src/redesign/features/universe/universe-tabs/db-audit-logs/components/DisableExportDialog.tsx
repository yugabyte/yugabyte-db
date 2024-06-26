import { FC } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { YBModal } from '../../../../../components';

import { auditLogStyles } from '../utils/AuditLogStyles';

interface DisableExportDialogProps {
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
  universeName: string;
}

export const DisableExportDialog: FC<DisableExportDialogProps> = ({
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
      title={t('dbAuitLog.disableExportTitle')}
      submitLabel={t('dbAuitLog.disableExportSubmitLabel')}
      cancelLabel={t('common.cancel')}
      onSubmit={onSubmit}
    >
      <Box className={classes.disableLogModalConatiner}>
        <Typography className={classes.exportInfoText}>
          {t('dbAuitLog.disableExportWarning')}
        </Typography>
        <Box className={classes.exportInfo}>
          <Typography className={classes.exportInfoText}>
            <Trans i18nKey={'dbAuitLog.disableExportMessage'} values={{ universeName }} />
          </Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
