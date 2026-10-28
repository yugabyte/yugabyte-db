import { FC } from 'react';
import { useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { Link } from 'react-router';
import clsx from 'clsx';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBModal, YBToggleField } from '@yugabyte-ui-library/core';
import { useManageCrossCloudFederation } from '../../../../../v2/api/universe/universe';
import { useYBToast } from '@app/redesign/features-v2/universe/create-universe/helpers/ToastUtils';
import { createErrorMessage, getPrimaryCluster } from '../../universe-form/utils/helpers';
import { Universe } from '../../universe-form/utils/dto';
import { PROVIDER_ROUTE_PREFIX } from '../../../../../components/configRedesign/providerRedesign/constants';

import InfoIcon from '../../../../assets/info-blue.svg';
import AlertIcon from '../../../../assets/alert.svg';

// Sizes and colours follow the Cross-Cloud Federated IAM Figma (section 4, universe "Actions").
const useStyles = makeStyles((theme) => ({
  content: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3)
  },
  toggleCard: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: theme.spacing(3),
    padding: theme.spacing(2),
    backgroundColor: '#FCFCFC',
    border: '1px solid #E5E5E9',
    borderRadius: 8
  },
  toggleText: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),
    flex: 1
  },
  toggleLabel: {
    color: '#151730',
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  subText: {
    color: theme.palette.grey[700],
    fontSize: 11.5,
    fontWeight: 400,
    lineHeight: '18px'
  },
  banner: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: theme.spacing(1),
    padding: theme.spacing(1),
    borderRadius: 8
  },
  info: {
    backgroundColor: theme.palette.info[100]
  },
  warning: {
    backgroundColor: theme.palette.warning[100],
    color: theme.palette.warning[700]
  },
  bannerText: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),
    padding: theme.spacing(0.5, 0),
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px'
  },
  icon: {
    flexShrink: 0
  },
  bannerTitle: {
    fontWeight: 600
  },
  providerLink: {
    color: theme.palette.primary[600],
    fontWeight: 600,
    textDecoration: 'underline'
  }
}));

interface CrossCloudFederationProvider {
  uuid: string;
  code: string;
  name: string;
  details?: { cloudInfo?: Record<string, { enableFederatedIam?: boolean } | undefined> };
}

interface CrossCloudFederationModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
  provider?: CrossCloudFederationProvider;
}

type CrossCloudFederationFormValues = {
  enabled: boolean;
};

export const CrossCloudFederationModal: FC<CrossCloudFederationModalProps> = ({
  open,
  onClose,
  universeData,
  provider
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.crossCloudFederation'
  });
  const { t: tCommon } = useTranslation();
  const classes = useStyles();
  const toast = useYBToast();
  const { universeDetails, universeUUID } = universeData;
  const isFederationConfigured =
    !!getPrimaryCluster(universeDetails)?.userIntent?.federationConfigured;
  // Enabling needs the provider's federation settings; disabling doesn't.
  const isProviderFederationEnabled =
    !!provider?.details?.cloudInfo?.[provider.code]?.enableFederatedIam;

  const { control, watch, handleSubmit } = useForm<CrossCloudFederationFormValues>({
    defaultValues: { enabled: isFederationConfigured }
  });
  const enabled = watch('enabled');
  const isEnabling = !isFederationConfigured && enabled;
  const isDisabling = isFederationConfigured && !enabled;

  const manageFederation = useManageCrossCloudFederation({
    mutation: {
      onSuccess: () => {
        toast.success(t(enabled ? 'enableSuccess' : 'disableSuccess'));
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  });

  const onSubmit = handleSubmit((values) =>
    manageFederation.mutate({ uniUUID: universeUUID, data: { enabled: values.enabled } })
  );

  const providerName = provider?.name ?? '';
  const providerLink = provider ? (
    <Link
      to={`/${PROVIDER_ROUTE_PREFIX}/${provider.code}/${provider.uuid}`}
      className={classes.providerLink}
    />
  ) : (
    <b />
  );

  return (
    <YBModal
      open={open}
      title={t('modalTitle')}
      submitLabel={tCommon('common.applyChanges')}
      cancelLabel={tCommon('common.cancel')}
      onClose={onClose}
      onSubmit={onSubmit}
      titleSeparator
      dialogContentProps={{ sx: { padding: '24px !important' } }}
      overrideHeight="auto"
      overrideWidth={600}
      submitTestId="CrossCloudFederationModal-Submit"
      cancelTestId="CrossCloudFederationModal-Cancel"
      buttonProps={{
        primary: {
          variant: 'ybaPrimary',
          dataTestId: 'CrossCloudFederationModal-Submit',
          disabled:
            (!isEnabling && !isDisabling) ||
            (isEnabling && !isProviderFederationEnabled) ||
            manageFederation.isLoading
        }
      }}
    >
      <Box className={classes.content} data-testid="CrossCloudFederationModal-Container">
        <Box className={classes.toggleCard}>
          <Box className={classes.toggleText}>
            <Typography className={classes.toggleLabel}>{t('toggleLabel')}</Typography>
            <Typography className={classes.subText}>{t('subText')}</Typography>
          </Box>
          <YBToggleField
            control={control}
            name="enabled"
            dataTestId="CrossCloudFederationModal-ToggleField"
            inputProps={{
              'data-testid': 'CrossCloudFederationModal-Toggle'
            }}
          />
        </Box>
        {/* Per the design, a banner shows only once the toggle moves away from the saved state. */}
        {isEnabling && isProviderFederationEnabled && (
          <Box className={clsx(classes.banner, classes.info)}>
            <InfoIcon width={24} height={24} className={classes.icon} />
            <Box className={classes.bannerText}>
              <span className={classes.bannerTitle}>{t('enableInfoTitle')}</span>
              <span>
                <Trans
                  t={t}
                  i18nKey="enableInfoText"
                  values={{ providerName }}
                  components={{ bold: <span className={classes.bannerTitle} /> }}
                />
              </span>
            </Box>
          </Box>
        )}
        {isEnabling && !isProviderFederationEnabled && (
          <Box className={clsx(classes.banner, classes.warning)}>
            <AlertIcon width={24} height={24} className={classes.icon} />
            <Box className={classes.bannerText}>
              <span className={classes.bannerTitle}>{t('providerDisabledTitle')}</span>
              <span>
                <Trans
                  t={t}
                  i18nKey="providerDisabledText"
                  values={{ providerName }}
                  components={{ providerLink }}
                />
              </span>
            </Box>
          </Box>
        )}
        {isDisabling && (
          <Box className={clsx(classes.banner, classes.warning)}>
            <AlertIcon width={24} height={24} className={classes.icon} />
            <Box className={classes.bannerText}>
              <span className={classes.bannerTitle}>{t('disableWarningTitle')}</span>
              <span>{t('disableWarningImpact')}</span>
              <span>{t('disableWarningPermissions')}</span>
            </Box>
          </Box>
        )}
      </Box>
    </YBModal>
  );
};
