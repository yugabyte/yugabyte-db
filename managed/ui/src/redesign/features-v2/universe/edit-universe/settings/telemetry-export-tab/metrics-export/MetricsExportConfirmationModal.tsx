import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';

import { YBModal, YBModalProps } from '@app/redesign/components';
import {
  METRICS_EXPORT_TRANSLATION_KEY_PREFIX,
  MetricsExportOperation
} from './metricsExportHelpers';

const MODAL_NAME = 'MetricsExportConfirmationModal';
const MODAL_WIDTH = 600;
const MODAL_HEIGHT = 274;

interface MetricsExportConfirmationModalProps {
  operation: MetricsExportOperation;
  universeName: string;
  isSubmitting: boolean;
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const useStyles = makeStyles((theme) => ({
  message: {
    color: theme.palette.grey[900],
    fontSize: 13,
    lineHeight: '20px'
  }
}));

export const MetricsExportConfirmationModal: FC<MetricsExportConfirmationModalProps> = ({
  operation,
  universeName,
  isSubmitting,
  onSubmit,
  modalProps
}) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: METRICS_EXPORT_TRANSLATION_KEY_PREFIX });

  return (
    <YBModal
      title={
        operation === 'create' ? t('confirmation.enableTitle') : t('confirmation.editTitle')
      }
      size="sm"
      overrideWidth={MODAL_WIDTH}
      overrideHeight={MODAL_HEIGHT}
      submitLabel={
        operation === 'create' ? t('confirmation.enableSubmit') : t('confirmation.editSubmit')
      }
      cancelLabel={t('confirmation.back')}
      onSubmit={onSubmit}
      isSubmitting={isSubmitting}
      buttonProps={{ primary: { disabled: isSubmitting } }}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Typography className={classes.message} variant="body2">
        <Trans
          t={t}
          i18nKey="confirmation.message"
          values={{ universeName }}
          components={{ bold: <b /> }}
        />
      </Typography>
    </YBModal>
  );
};
