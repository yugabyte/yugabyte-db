import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, YBModal } from '@yugabyte-ui-library/core';

const { Box, Typography } = mui;

interface SwitchToGuidedConfirmModalProps {
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

export const SwitchToGuidedConfirmModal: FC<SwitchToGuidedConfirmModalProps> = ({
  open,
  onClose,
  onSubmit
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.switchToGuidedModal'
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      onSubmit={onSubmit}
      title={t('title')}
      titleSeparator
      overrideWidth={600}
      overrideHeight={304}
      cancelLabel={t('cancel')}
      cancelTestId="switch-to-guided-cancel"
      submitLabel={t('confirm')}
      submitTestId="switch-to-guided-confirm"
      dialogContentProps={{ sx: { padding: '16px !important' } }}
      buttonProps={{
        primary: {
          variant: 'ybaPrimary',
          dataTestId: 'switch-to-guided-confirm'
        }
      }}
    >
      <Box
        sx={{
          border: (theme) => `1px solid ${theme.palette.grey[200]}`,
          borderRadius: '8px',
          display: 'flex',
          flexDirection: 'column',
          gap: '8px',
          padding: '16px'
        }}
      >
        <Typography variant="body2" sx={{ fontWeight: 600 }}>
          {t('unsupportedTitle')}
        </Typography>
        <Typography variant="body2">{t('unsupportedDescription')}</Typography>
      </Box>
    </YBModal>
  );
};
