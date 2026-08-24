import { FC, MouseEvent } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';
import { useSelector } from 'react-redux';
import { YBButton, YBModal, mui } from '@yugabyte-ui-library/core';

import BoltIcon from '@app/redesign/assets/what-changed/bolt.svg';
import MapPinIcon from '@app/redesign/assets/what-changed/map-pin.svg';
import BulletIcon from '@app/redesign/assets/what-changed/bullet.svg';
import NavArrowIcon from '@app/redesign/assets/what-changed/nav-arrow.svg';
import SortIcon from '@app/redesign/assets/what-changed/sort.svg';
import UserGroupIcon from '@app/redesign/assets/user-group.svg';
import { EDIT_RUNTIME_CONFIG_QUERY_PARAM, RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { isCurrentUserSuperAdmin } from '../helper-methods';
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
  RolloutBanner,
  RuntimeConfigTag,
  SectionCard,
  SectionTitle,
  TableHeader,
  TableRow,
  UserGroupWrap
} from './HelperComponent';

const { Box, Typography } = mui;

/** Figma node 17666:10129 */
const MODAL_WIDTH_PX = 800;
const MODAL_HEIGHT_PX = 'auto';
const SECTION_CARD_GAP_PX = 24;
const FIND_OUT_MORE_BG = '#7879F1';
const GLOBAL_RUNTIME_CONFIG_PATH = '/admin/advanced/global-config';

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
  const currentUserInfo = useSelector((state: any) => state.customer.currentUser.data);
  const isSuperAdmin = isCurrentUserSuperAdmin(currentUserInfo?.role);

  const handleFindOutMore = () => {
    if (onFindOutMore) {
      onFindOutMore();
      return;
    }
    window.open(releaseNotesUrl, '_blank', 'noopener,noreferrer');
    onClose();
  };

  const handleOpenGlobalRuntimeConfig = (event: MouseEvent) => {
    event.preventDefault();
    onClose();
    const editKey = encodeURIComponent(RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI);
    browserHistory.push(
      `${GLOBAL_RUNTIME_CONFIG_PATH}?${EDIT_RUNTIME_CONFIG_QUERY_PARAM}=${editKey}`
    );
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('title')}
      titleSeparator
      size="xl"
      overrideWidth={MODAL_WIDTH_PX}
      overrideHeight={MODAL_HEIGHT_PX}
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
            variant="primary"
            size="large"
            onClick={handleFindOutMore}
            dataTestId="what-changed-modal-find-out-more"
            sx={{
              backgroundColor: FIND_OUT_MORE_BG,
              borderColor: FIND_OUT_MORE_BG,
              '&:hover, &:focus': {
                backgroundColor: FIND_OUT_MORE_BG,
                borderColor: FIND_OUT_MORE_BG
              }
            }}
          >
            {t('findOutMore')}
          </YBButton>
        </FooterActions>
      }
    >
      <ModalBody>
        <SectionCard sx={{ gap: `${SECTION_CARD_GAP_PX}px` }}>
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
                      fontFamily: 'Inter',
                      fontSize: '13px',
                      fontWeight: 600,
                      lineHeight: '16px',
                      color: '#0B1117'
                    }}
                  >
                    {t(item.titleKey)}
                  </Typography>
                  <Typography
                    sx={{
                      fontFamily: 'Inter',
                      fontSize: '13px',
                      fontWeight: 400,
                      lineHeight: '16px',
                      color: '#4E5F6D'
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
                fontFamily: 'Inter',
                fontSize: '13px',
                fontWeight: 400,
                lineHeight: '16px',
                color: '#4E5F6D'
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

        <SectionCard sx={{ gap: `${SECTION_CARD_GAP_PX}px` }}>
          <Box sx={{ display: 'flex', gap: '16px', alignItems: 'center' }}>
            <MapWrap>
              <MapPinIcon />
            </MapWrap>
            <SectionTitle>{t('sameFeaturesNewHome')}</SectionTitle>
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
              <TableRow key={`${row.thenKey}-${row.nowPathKey}-${row.thenDetailKey ?? ''}`}>
                <Typography
                  sx={{
                    fontFamily: 'Inter',
                    fontSize: '13px',
                    fontWeight: 400,
                    lineHeight: '32px',
                    color: '#0B1117',
                    whiteSpace: 'nowrap'
                  }}
                >
                  {t(row.thenKey)}
                  {row.thenDetailKey ? (
                    <Box
                      component="span"
                      sx={{
                        color: '#4E5F6D',
                        fontSize: '11.5px',
                        fontWeight: 400,
                        lineHeight: '32px',
                        ml: '8px'
                      }}
                    >
                      {t(row.thenDetailKey)}
                    </Box>
                  ) : null}
                </Typography>
                <ArrowWrap>
                  <NavArrowIcon />
                </ArrowWrap>
                <Typography
                  sx={{
                    fontFamily: 'Inter',
                    fontSize: '13px',
                    fontWeight: 400,
                    lineHeight: '32px',
                    color: '#0B1117',
                    whiteSpace: 'nowrap'
                  }}
                >
                  <Box component="span" sx={{ color: '#6D7C88', fontWeight: 400 }}>
                    {t('settings')}
                  </Box>
                  <Box component="span" sx={{ color: '#0B1117', fontWeight: 600 }}>
                    {`  /  ${t(row.nowPathKey)}`}
                  </Box>
                </Typography>
              </TableRow>
            ))}
          </RelocationTable>
        </SectionCard>

        {isSuperAdmin && (
          <RolloutBanner>
            <Box sx={{ display: 'flex', gap: '16px', alignItems: 'center' }}>
              <UserGroupWrap>
                <UserGroupIcon />
              </UserGroupWrap>
              <Typography
                sx={{
                  fontFamily: 'Inter',
                  fontSize: '13px',
                  fontWeight: 600,
                  lineHeight: '16px',
                  color: '#4E5F6D'
                }}
              >
                {t('rolloutTitle')}
              </Typography>
            </Box>
            <Typography
              sx={{
                fontFamily: 'Inter',
                fontSize: '13px',
                fontWeight: 400,
                lineHeight: '18px',
                color: '#4E5F6D'
              }}
            >
              <Trans
                t={t}
                i18nKey="rolloutBody"
                values={{ runtimeConfig: RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI }}
                components={{
                  configTag: <RuntimeConfigTag component="span" />,
                  globalConfigLink: (
                    <LearnMoreLink
                      href={`${GLOBAL_RUNTIME_CONFIG_PATH}?${EDIT_RUNTIME_CONFIG_QUERY_PARAM}=${encodeURIComponent(
                        RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI
                      )}`}
                      onClick={handleOpenGlobalRuntimeConfig}
                    />
                  )
                }}
              />
            </Typography>
          </RolloutBanner>
        )}
      </ModalBody>
    </YBModal>
  );
};
