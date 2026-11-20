import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  AlertVariant,
  mui,
  yba,
  YBAlert,
  YBCheckbox,
  YBRadio,
  YBTag,
  YBTooltip
} from '@yugabyte-ui-library/core';

import { YBTimeFormats, ybFormatDate } from '@app/redesign/helpers/DateUtils';
import { SupportBundle } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import InfoNewIcon from '@app/redesign/assets/approved/info-new.svg';
import AlertIcon from '@app/redesign/assets/approved/alert.svg';

import { getSensitiveComponents } from '../model/bundleDisplay';
import { downloadSupportBundle } from '../supportBundleApi';

const { styled, Box, Typography, useTheme } = mui;
const { YBModal } = yba;

const TRANSLATION_KEY_PREFIX = 'universeActions.supportBundleV2';

const Summary = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(1.5),
  padding: theme.spacing(2),
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: 8,
  background: theme.palette.grey[50]
}));

const MetaRow = styled(Box)(({ theme }) => ({
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  gap: theme.spacing(2),
  '&:not(:last-of-type)': {
    paddingBottom: theme.spacing(1.5),
    borderBottom: `1px solid ${theme.palette.grey[300]}`
  }
}));

const MetaLabel = styled(Typography)(({ theme }) => ({
  color: theme.palette.grey[700],
  whiteSpace: 'nowrap'
}));

/** Lines the warning icon up with the first line of text, as the design does. */
const SensitiveWarning = styled(YBAlert)(({ theme }) => ({
  alignItems: 'flex-start',
  padding: theme.spacing(2)
}));

const DestinationCard = styled(Box)(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  gap: theme.spacing(1),
  padding: theme.spacing(2),
  border: `1px solid ${theme.palette.primary[400]}`,
  borderRadius: 8,
  background: theme.palette.primary[100]
}));

const RadioSlot = styled(Box)({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  width: 24,
  height: 24
});

interface ShareBundleModalProps {
  universeUuid: string;
  /** The modal is open while a bundle is set. */
  bundle: SupportBundle | null;
  onClose: () => void;
}

/**
 * Download is the only destination: the v2 API can't attach a bundle to a Zendesk ticket or
 * upload it to object storage yet.
 */
export const ShareBundleModal = ({ universeUuid, bundle, onClose }: ShareBundleModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });
  const theme = useTheme();
  const [isAcknowledged, setIsAcknowledged] = useState(false);
  const sensitiveComponents = bundle ? getSensitiveComponents(bundle) : [];
  const needsAcknowledgement = sensitiveComponents.length > 0;

  const handleClose = () => {
    setIsAcknowledged(false);
    onClose();
  };
  const onSubmit = () => {
    if (bundle?.info?.uuid) {
      downloadSupportBundle(universeUuid, bundle.info.uuid, true);
    }
    handleClose();
  };

  return (
    <YBModal
      open={!!bundle}
      onClose={handleClose}
      onSubmit={onSubmit}
      title={t('share.title')}
      submitLabel={t('share.download')}
      cancelLabel={tCommon('cancel')}
      submitTestId="ShareBundleModal-Submit"
      cancelTestId="ShareBundleModal-Cancel"
      buttonProps={{
        primary: {
          disabled: needsAcknowledgement && !isAcknowledged,
          dataTestId: 'ShareBundleModal-Submit'
        }
      }}
      size="sm"
      overrideHeight="fit-content"
      titleSeparator
    >
      <Box display="flex" flexDirection="column" gap={3} padding={1}>
        {needsAcknowledgement && (
          <SensitiveWarning
            open
            variant={AlertVariant.Warning}
            dataTestId="ShareBundleModal-SensitiveWarning"
            text={
              <Box display="flex" flexDirection="column" gap={2}>
                <Typography variant="body2">
                  <b>{t('share.sensitiveWarningTitle')}</b> {t('share.sensitiveWarningBody')}
                </Typography>
                <YBCheckbox
                  checked={isAcknowledged}
                  onChange={(event) => setIsAcknowledged(event.target.checked)}
                  label={t('share.acknowledge')}
                  dataTestId="ShareBundleModal-Acknowledge"
                />
              </Box>
            }
          />
        )}
        <Summary>
          <MetaRow>
            <MetaLabel variant="body2">{t('share.timeRange')}</MetaLabel>
            <Typography variant="body2" fontWeight={500} textAlign="right">
              {bundle?.spec?.start_date &&
                ybFormatDate(bundle.spec.start_date, YBTimeFormats.YB_SHORT_DATETIME)}
              {`\u00a0 ${t('share.to')} \u00a0`}
              {bundle?.spec?.end_date &&
                ybFormatDate(bundle.spec.end_date, YBTimeFormats.YB_SHORT_DATETIME)}
            </Typography>
          </MetaRow>
          {/* Bundles don't report a redaction outcome yet, so the row shows only what the spec
              tells us: components that are never redacted. */}
          {needsAcknowledgement && (
            <MetaRow>
              <Box display="flex" alignItems="center" gap={0.5}>
                <MetaLabel variant="body2">{t('share.redaction')}</MetaLabel>
                <YBTooltip title={t('list.redactionTooltip')}>
                  <Box display="flex" color="grey.500">
                    <InfoNewIcon />
                  </Box>
                </YBTooltip>
              </Box>
              <YBTooltip title={t('share.unredactedTooltip')}>
                <span data-testid="ShareBundleModal-Unredacted">
                  <YBTag
                    size="medium"
                    variant="light"
                    startIcon={<AlertIcon />}
                    // The tag styles icons through a nested `svg` rule, so color and size must go
                    // through startIconSx, as plain CSS values.
                    startIconSx={{ width: 16, height: 16, color: theme.palette.warning[700] }}
                  >
                    {t('share.unredactedIncluded')}
                  </YBTag>
                </span>
              </YBTooltip>
            </MetaRow>
          )}
        </Summary>
        <Box display="flex" flexDirection="column" gap={1.5}>
          <Typography
            variant="subtitle1"
            fontWeight={500}
            color="grey.700"
            textTransform="uppercase"
          >
            {t('share.destination')}
          </Typography>
          <DestinationCard>
            <RadioSlot>
              <YBRadio
                checked
                inputProps={{ 'aria-label': t('share.download') }}
                dataTestId="ShareBundleModal-Download"
              />
            </RadioSlot>
            <Typography variant="body1" lineHeight="16px">
              {t('share.download')}
            </Typography>
          </DestinationCard>
        </Box>
      </Box>
    </YBModal>
  );
};
