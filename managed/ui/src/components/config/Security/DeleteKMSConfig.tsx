import { FC } from 'react';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBAlert, YBModal, AlertVariant } from '../../../redesign/components';

export const useDeleteKmsStyles = makeStyles(() => ({
  errorIcon: {
    color: '#E73E36'
  },
  deleteKmsText: {
    fontSize: 14
  },
  alertText: {
    lineHeight: '20px'
  },
  confirmTextConfigName: {
    fontWeight: 700,
    fontSize: 14
  }
}));

interface DeleteKMSConfigProps {
  open: boolean;
  onHide: () => void;
  onSubmit: (config: Record<string, any>) => void;
  config: Record<string, any>;
}

export const DeleteKMSConfig: FC<DeleteKMSConfigProps> = ({ config, open, onHide, onSubmit }) => {
  const classes = useDeleteKmsStyles();
  const { t } = useTranslation();

  const DELETE_CONFIG_WARNING = (
    <Trans i18nKey={'config.security.encryptionAtRest.deleteConfigWarning'}>
      <br />
    </Trans>
  );

  const DELETE_CONFIRM_TEXT = (
    <Trans i18nKey={'config.security.encryptionAtRest.deleteConfigConfirmText'}>
      <strong className={classes.confirmTextConfigName}>
        {{
          configName: `"${config.metadata.name}"`
        }}
      </strong>
    </Trans>
  );

  return (
    <YBModal
      overrideHeight="auto"
      overrideWidth={600}
      titleSeparator
      open={open}
      onClose={onHide}
      title={t('config.security.encryptionAtRest.deleteKmsConfig')}
      submitLabel={t('config.security.encryptionAtRest.deleteKmsConfig')}
      cancelLabel={t('common.cancel')}
      onSubmit={() => onSubmit(config)}
      submitTestId="DeleteKMS-Submit"
      cancelTestId="DeleteKMS-Close"
    >
      <Box mt={2}>
        <YBAlert
          open={true}
          icon={<></>}
          text={
            <Box display="flex" py={1}>
              <Box mr={1} className={classes.errorIcon}>
                <i className="fa fa-exclamation-triangle" aria-hidden="true"></i>
              </Box>
              <Box>
                <Typography
                  variant="body2"
                  className={clsx(classes.deleteKmsText, classes.alertText)}
                >
                  {DELETE_CONFIG_WARNING}
                </Typography>
              </Box>
            </Box>
          }
          variant={AlertVariant.Error}
        />

        <Box mt={3} mb={5} ml={1}>
          <Typography variant="body2" className={classes.deleteKmsText}>
            {DELETE_CONFIRM_TEXT}
          </Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
