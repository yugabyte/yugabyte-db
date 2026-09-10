import { FC } from 'react';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';

import { YBModal, YBModalProps } from '@app/redesign/components';

import InfoIcon from '@app/redesign/assets/info-blue.svg';
import ErrorIcon from '@app/redesign/assets/error-circle.svg';

const MODAL_NAME = 'TelemetryConfigConfirmationModal';
const MODAL_WIDTH = 600;
const MODAL_HEIGHT_ENABLE = 274;
const MODAL_HEIGHT_DISABLE = 290;
const TRANSLATION_KEY_PREFIX = 'editUniverse.telemetryConfigConfirmation';

export type TelemetryConfigType =
  | 'queryLog'
  | 'auditLog'
  | 'metricsExport'
  | 'queryLogExport'
  | 'auditLogExport';

export type TelemetryConfigConfirmationOperation = 'enable' | 'disable' | 'edit';

interface TelemetryConfigConfirmationModalProps {
  configType: TelemetryConfigType;
  operation: TelemetryConfigConfirmationOperation;
  universeName: string;
  replicationFactor: number;
  isSubmitting: boolean;
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const FEATURE_NAME_KEYS: Record<TelemetryConfigType, string> = {
  queryLog: 'featureNames.queryLog',
  auditLog: 'featureNames.auditLog',
  metricsExport: 'featureNames.metricsExport',
  queryLogExport: 'featureNames.queryLogExport',
  auditLogExport: 'featureNames.auditLogExport'
};

const FEATURE_NAME_LOWER_KEYS: Record<TelemetryConfigType, string> = {
  queryLog: 'featureNamesLower.queryLog',
  auditLog: 'featureNamesLower.auditLog',
  metricsExport: 'featureNamesLower.metricsExport',
  queryLogExport: 'featureNamesLower.queryLogExport',
  auditLogExport: 'featureNamesLower.auditLogExport'
};

const useStyles = makeStyles((theme) => ({
  content: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3)
  },
  message: {
    color: theme.palette.grey[900],
    fontSize: 13,
    lineHeight: '20px'
  },
  inlineMessage: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: theme.spacing(1),

    width: '100%',
    padding: theme.spacing(1),

    borderRadius: theme.shape.borderRadius
  },
  info: {
    backgroundColor: theme.palette.info[100]
  },
  error: {
    backgroundColor: theme.palette.error[100]
  },
  inlineMessageIcon: {
    flexShrink: 0,

    width: 24,
    height: 24
  },
  inlineMessageText: {
    flex: 1,

    paddingTop: theme.spacing(0.5),
    paddingBottom: theme.spacing(0.5),

    color: theme.palette.grey[900],
    fontSize: 13,
    lineHeight: '16px'
  }
}));

export const TelemetryConfigConfirmationModal: FC<TelemetryConfigConfirmationModalProps> = ({
  configType,
  operation,
  universeName,
  replicationFactor,
  isSubmitting,
  onSubmit,
  modalProps
}) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const requiresDowntime = replicationFactor <= 1;
  const featureName = t(FEATURE_NAME_KEYS[configType]);
  const featureNameLower = t(FEATURE_NAME_LOWER_KEYS[configType]);
  const translationValues = { featureName, featureNameLower, universeName };

  const getTitle = () => {
    if (operation === 'disable') {
      return t('disableTitle', { featureName });
    }
    if (operation === 'edit') {
      return t('editTitle');
    }
    return t('enableTitle', { featureName });
  };

  const getSubmitLabel = () => {
    if (requiresDowntime) {
      return t('acceptDowntimeProceed');
    }
    if (operation === 'disable') {
      return t('disableSubmit', { featureName });
    }
    if (operation === 'edit') {
      return t('editSubmit');
    }
    return t('enableSubmit', { featureName });
  };

  const renderBody = () => {
    if (operation === 'disable') {
      return (
        <div className={classes.content}>
          <Typography className={classes.message} variant="body2">
            <Trans
              t={t}
              i18nKey="disableConfirmMessage"
              values={translationValues}
              components={{ bold: <b /> }}
            />
          </Typography>
          <div
            className={clsx(classes.inlineMessage, requiresDowntime ? classes.error : classes.info)}
          >
            {requiresDowntime ? (
              <ErrorIcon className={classes.inlineMessageIcon} />
            ) : (
              <InfoIcon className={classes.inlineMessageIcon} />
            )}
            <Typography className={classes.inlineMessageText} variant="body2">
              <Trans
                t={t}
                i18nKey={requiresDowntime ? 'disableNoteRestart' : 'disableNoteRolling'}
                values={translationValues}
                components={{ bold: <b /> }}
              />
            </Typography>
          </div>
        </div>
      );
    }

    if (requiresDowntime) {
      return (
        <div className={classes.content}>
          <Typography className={classes.message} variant="body2">
            <Trans
              t={t}
              i18nKey="enableMessageRestart"
              values={translationValues}
              components={{ bold: <b /> }}
            />
          </Typography>
          <div className={clsx(classes.inlineMessage, classes.error)}>
            <ErrorIcon className={classes.inlineMessageIcon} />
            <Typography className={classes.inlineMessageText} variant="body2">
              <Trans t={t} i18nKey="enableDowntimeNote" components={{ bold: <b /> }} />
            </Typography>
          </div>
        </div>
      );
    }

    return (
      <Typography className={classes.message} variant="body2">
        <Trans
          t={t}
          i18nKey="enableMessageRolling"
          values={translationValues}
          components={{ bold: <b /> }}
        />
      </Typography>
    );
  };

  const modalHeight = operation === 'disable' ? MODAL_HEIGHT_DISABLE : MODAL_HEIGHT_ENABLE;

  return (
    <YBModal
      title={getTitle()}
      size="sm"
      overrideWidth={MODAL_WIDTH}
      overrideHeight={modalHeight}
      submitLabel={getSubmitLabel()}
      cancelLabel={t('back')}
      onSubmit={onSubmit}
      isSubmitting={isSubmitting}
      buttonProps={{ primary: { disabled: isSubmitting } }}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      {renderBody()}
    </YBModal>
  );
};
