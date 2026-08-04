import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { YBButton, YBModal, mui, YBProductTour } from '@yugabyte-ui-library/core';

import BoltIcon from '@app/redesign/assets/what-changed/bolt.svg';
import MapPinIcon from '@app/redesign/assets/what-changed/map-pin.svg';
import BulletIcon from '@app/redesign/assets/what-changed/bullet.svg';
import NavArrowIcon from '@app/redesign/assets/what-changed/nav-arrow.svg';
import SortIcon from '@app/redesign/assets/what-changed/sort.svg';
import {
  ArrowWrap,
  BoltWrap,
  BulletWrap,
  DEFAULT_RELEASE_NOTES_URL,
  FeatureList,
  FeatureRow,
  FooterActions,
  GradientTitle,
  HeaderLabel,
  LearnMoreLink,
  MapWrap,
  ModalBody,
  RelocationTable,
  SectionCard,
  SectionTitle,
  TableHeader,
  TableRow
} from './HelperComponent';

const { Box, Typography } = mui;

export const WHAT_CHANGED_MODAL_DISMISS_KEY = 'yb_what_changed_modal_dismissed';

interface WhatChangedModalProps {
  open: boolean;
  onClose: () => void;
  onFindOutMore?: () => void;
  releaseNotesUrl?: string;
}

interface FeatureItem {
  titleKey: string;
  descriptionKey: string;
  learnMoreHref?: string;
}

interface RelocationRow {
  thenKey: string;
  thenDetailKey?: string;
  nowPathKey: string;
}

const FEATURE_ITEMS: FeatureItem[] = [
  {
    titleKey: 'enhancedUniverseConfigTitle',
    descriptionKey: 'enhancedUniverseConfigDescription',
    learnMoreHref: DEFAULT_RELEASE_NOTES_URL
  },
  {
    titleKey: 'centralizedSettingsTitle',
    descriptionKey: 'centralizedSettingsDescription',
    learnMoreHref: DEFAULT_RELEASE_NOTES_URL
  },
  {
    titleKey: 'preferredAzRankingTitle',
    descriptionKey: 'preferredAzRankingDescription',
    learnMoreHref: DEFAULT_RELEASE_NOTES_URL
  }
];

const RELOCATION_ROWS: RelocationRow[] = [
  {
    thenKey: 'editUniverse',
    thenDetailKey: 'editUniversePlacementDetail',
    nowPathKey: 'nowPlacement'
  },
  {
    thenKey: 'editUniverse',
    thenDetailKey: 'editUniverseHardwareDetail',
    nowPathKey: 'nowHardware'
  },
  {
    thenKey: 'logs',
    nowPathKey: 'nowLogs'
  },
  {
    thenKey: 'logsAndMetricsExport',
    nowPathKey: 'nowTelemetryExport'
  }
];

export const WhatChangedModal: FC<WhatChangedModalProps> = ({
  open,
  onClose,
  onFindOutMore,
  releaseNotesUrl = DEFAULT_RELEASE_NOTES_URL
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.whatChangedModal'
  });

  const handleFindOutMore = () => {
    if (onFindOutMore) {
      onFindOutMore();
      return;
    }
    window.open(releaseNotesUrl, '_blank', 'noopener,noreferrer');
    onClose();
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('title')}
      titleSeparator
      size="xl"
      overrideWidth={800}
      overrideHeight="auto"
      hideCloseBtn={false}
      // Above OnBoardingBanner (2100) and HighlightedStatsPanel (2040).
      sx={{ zIndex: 2300 }}
      // yba YBModal only mounts actions when submit/cancel labels are set;
      // customButtonTemplate then replaces the default submit control.
      submitLabel={t('findOutMore')}
      dialogContentProps={{
        dividers: false,
        sx: {
          padding: '0 !important',
          backgroundColor: '#FBFCFD'
        }
      }}
      customButtonTemplate={
        <FooterActions>
          <YBButton
            variant="secondary"
            size="large"
            onClick={onClose}
            dataTestId="what-changed-modal-close"
          >
            {t('close')}
          </YBButton>
          <YBButton
            variant="gradient"
            size="large"
            onClick={handleFindOutMore}
            dataTestId="what-changed-modal-find-out-more"
          >
            {t('findOutMore')}
          </YBButton>
        </FooterActions>
      }
    >
      <ModalBody>
        <SectionCard>
          <Box sx={{ display: 'flex', gap: '16px', alignItems: 'center' }}>
            <BoltWrap>
              <BoltIcon />
            </BoltWrap>
            <GradientTitle>{t('newAndImproved')}</GradientTitle>
          </Box>

          <FeatureList>
            {FEATURE_ITEMS.map((item) => (
              <FeatureRow key={item.titleKey}>
                <BulletWrap>
                  <BulletIcon />
                </BulletWrap>
                <Box sx={{ display: 'flex', flexDirection: 'column', gap: '6px', flex: 1 }}>
                  <Typography
                    sx={{
                      fontSize: '13px',
                      fontWeight: 600,
                      lineHeight: '16px',
                      color: 'grey.900'
                    }}
                  >
                    {t(item.titleKey)}
                  </Typography>
                  <Typography
                    sx={{
                      fontSize: '13px',
                      fontWeight: 400,
                      lineHeight: '16px',
                      color: 'grey.700'
                    }}
                  >
                    <Trans
                      t={t}
                      i18nKey={item.descriptionKey}
                      components={{
                        learnMore: (
                          <LearnMoreLink
                            href={item.learnMoreHref ?? releaseNotesUrl}
                            target="_blank"
                            rel="noopener noreferrer"
                          />
                        )
                      }}
                    />
                  </Typography>
                </Box>
              </FeatureRow>
            ))}
          </FeatureList>

          <Box sx={{ pl: '40px' }}>
            <Typography
              sx={{
                fontSize: '13px',
                fontWeight: 400,
                lineHeight: '16px',
                color: 'grey.700'
              }}
            >
              <Trans
                t={t}
                i18nKey="otherImprovements"
                components={{
                  releaseLink: (
                    <LearnMoreLink
                      href={releaseNotesUrl}
                      target="_blank"
                      rel="noopener noreferrer"
                    />
                  )
                }}
              />
            </Typography>
          </Box>
        </SectionCard>

        <SectionCard>
          <Box sx={{ display: 'flex', gap: '16px', alignItems: 'flex-start' }}>
            <MapWrap>
              <MapPinIcon />
            </MapWrap>
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
              <SectionTitle>{t('whereDidFeaturesMove')}</SectionTitle>
              <Typography
                sx={{
                  fontSize: '13px',
                  fontWeight: 400,
                  lineHeight: '16px',
                  color: 'grey.700'
                }}
              >
                {t('whereDidFeaturesMoveDescription')}
              </Typography>
            </Box>
          </Box>

          <RelocationTable>
            <TableHeader>
              <HeaderLabel>
                {t('then')}
                <SortIcon />
              </HeaderLabel>
              <Box />
              <HeaderLabel>
                {t('now')}
                <SortIcon />
              </HeaderLabel>
            </TableHeader>
            {RELOCATION_ROWS.map((row) => (
              <TableRow key={`${row.thenKey}-${row.nowPathKey}`}>
                <Typography
                  sx={{
                    fontSize: '13px',
                    fontWeight: 400,
                    lineHeight: '32px',
                    color: 'grey.900',
                    whiteSpace: 'nowrap'
                  }}
                >
                  {t(row.thenKey)}
                  {row.thenDetailKey ? (
                    <Box component="span" sx={{ color: 'grey.700', fontSize: '11.5px', ml: '8px' }}>
                      {t(row.thenDetailKey)}
                    </Box>
                  ) : null}
                </Typography>
                <ArrowWrap>
                  <NavArrowIcon />
                </ArrowWrap>
                <Typography
                  sx={{
                    fontSize: '13px',
                    fontWeight: 400,
                    lineHeight: '32px',
                    color: 'grey.900',
                    whiteSpace: 'nowrap'
                  }}
                >
                  <Box component="span" sx={{ color: '#6D7C88' }}>
                    {t('settings')}
                  </Box>
                  {`  /  ${t(row.nowPathKey)}`}
                </Typography>
              </TableRow>
            ))}
          </RelocationTable>
        </SectionCard>
      </ModalBody>
    </YBModal>
  );
};
