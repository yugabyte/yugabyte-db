import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';

import { YBModal, YBModalProps } from '@app/redesign/components';

const TRANSLATION_KEY_PREFIX = 'editUniverse.logs.navigateToTelemetryExportConfirmation';
const MODAL_NAME = 'NavigateToTelemetryExportConfirmationModal';
const MODAL_WIDTH = 400;
const MODAL_HEIGHT = 274;

interface NavigateToTelemetryExportConfirmationModalProps {
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

export const NavigateToTelemetryExportConfirmationModal: FC<NavigateToTelemetryExportConfirmationModalProps> = ({
  onSubmit,
  modalProps
}) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  return (
    <YBModal
      title={t('title')}
      size="sm"
      overrideWidth={MODAL_WIDTH}
      overrideHeight={MODAL_HEIGHT}
      submitLabel={t('submit')}
      cancelLabel={t('back')}
      onSubmit={onSubmit}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Typography className={classes.message} variant="body2">
        <Trans t={t} i18nKey="message" components={{ bold: <b /> }} />
      </Typography>
    </YBModal>
  );
};
