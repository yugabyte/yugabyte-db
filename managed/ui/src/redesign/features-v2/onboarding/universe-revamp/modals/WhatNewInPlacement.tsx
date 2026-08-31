import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { YBButton, YBModal, mui } from '@yugabyte-ui-library/core';

import IdeaIcon from '@app/redesign/assets/what-new-placement/idea.svg';
import CalloutArrowIcon from '@app/redesign/assets/what-new-placement/callout-arrow.svg';
import StepCounterIcon from '@app/redesign/assets/what-new-placement/step-counter.svg';
import YBLogoIcon from '@app/redesign/assets/what-new-placement/yb-logo.svg';
import {
  CalloutPill,
  DEFAULT_RELEASE_NOTES_URL,
  FooterActions,
  HeroCard,
  InfoTipBox,
  OnboardingModalContent,
  TipIconWrap
} from './HelperComponent';

const { Box, Typography, styled } = mui;

interface WhatNewInPlacementProps {
  open: boolean;
  onClose: () => void;
  onFindOutMore?: () => void;
  releaseNotesUrl?: string;
}

const PreviewPanel = styled(Box)(({ theme }) => ({
  position: 'absolute',
  top: '29px',
  right: '1px',
  width: '372px',
  height: '270px',
  display: 'flex',
  flexDirection: 'column',
  overflow: 'hidden',
  borderRadius: '8px',
  border: `1px solid ${theme.palette.grey[200]}`,
  backgroundColor: theme.palette.common.white
}));

const PreviewHeader = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: '16px',
  height: '48px',
  padding: '8px 24px 8px 20px',
  backgroundColor: '#F7F9FB',
  borderBottom: '1px solid #E9EEF2',
  '& > svg': {
    width: '18px',
    height: '18px',
    display: 'block',
    flexShrink: 0
  }
}));

const PreviewBody = styled(Box)(() => ({
  display: 'flex',
  flex: 1,
  minHeight: 0,
  backgroundColor: '#FBFCFD'
}));

const StepperPanel = styled(Box)(() => ({
  position: 'relative',
  display: 'flex',
  flexDirection: 'column',
  width: '296px',
  borderRight: '1px solid #E9EEF2',
  backgroundColor: '#FBFCFD'
}));

const StepSectionTitle = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  height: '64px',
  padding: '0 24px',
  fontSize: '13px',
  fontWeight: 600,
  lineHeight: '16px',
  color: '#0B1117'
}));

const StepRow = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: '16px',
  height: '48px',
  padding: '0 16px 0 24px'
}));

const StepCounterWrap = styled(Box)(() => ({
  position: 'relative',
  width: '32px',
  height: '32px',
  flexShrink: 0,
  '& > svg': {
    width: '32px',
    height: '32px',
    display: 'block'
  }
}));

const StepNumber = styled(Typography)(() => ({
  position: 'absolute',
  inset: 0,
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '16px',
  color: '#6D7C88'
}));

const StepConnector = styled(Box)(() => ({
  position: 'absolute',
  // Align between step counter centers (24px pad + 16px radius).
  left: '39px',
  top: '88px',
  width: '1px',
  height: '32px',
  backgroundColor: '#E9EEF2'
}));
const CalloutRow = styled(Box)(() => ({
  position: 'absolute',
  display: 'flex',
  alignItems: 'center',
  gap: '10px',
  '& > svg': {
    width: '46px',
    height: '7px',
    display: 'block',
    flexShrink: 0
  }
}));

const STEPS = [
  { number: '1', labelKey: 'stepRegions' },
  { number: '2', labelKey: 'stepAvailabilityZones' }
] as const;

export const WhatNewInPlacement: FC<WhatNewInPlacementProps> = ({
  open,
  onClose,
  onFindOutMore,
  releaseNotesUrl = DEFAULT_RELEASE_NOTES_URL
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.whatNewInPlacement'
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
      // Above EditPlacement fullscreen shell (z-index 1299).
      sx={{ zIndex: 1400 }}
      // yba YBModal only mounts actions when submit/cancel labels are set;
      // customButtonTemplate then replaces the default submit control.
      submitLabel={t('findOutMore')}
      dialogContentProps={{
        dividers: false,
        sx: {
          padding: '0 !important',
          backgroundColor: '#FFFFFF'
        }
      }}
      customButtonTemplate={
        <FooterActions>
          <YBButton
            variant="secondary"
            size="large"
            onClick={onClose}
            dataTestId="what-new-in-placement-modal-close"
          >
            {t('close')}
          </YBButton>
          <YBButton
            variant="gradient"
            size="large"
            onClick={handleFindOutMore}
            dataTestId="what-new-in-placement-modal-find-out-more"
          >
            {t('findOutMore')}
          </YBButton>
        </FooterActions>
      }
    >
      <OnboardingModalContent>
        <HeroCard>
          <PreviewPanel>
            <PreviewHeader>
              <YBLogoIcon />
              <Typography
                sx={{
                  fontSize: '15px',
                  fontWeight: 600,
                  lineHeight: '24px',
                  color: '#6D7C88'
                }}
              >
                {t('editPlacement')}
              </Typography>
            </PreviewHeader>
            <PreviewBody>
              <StepperPanel>
                <StepSectionTitle>{t('placement')}</StepSectionTitle>
                <StepConnector />
                {STEPS.map((step) => (
                  <StepRow key={step.number}>
                    <StepCounterWrap>
                      <StepCounterIcon />
                      <StepNumber>{step.number}</StepNumber>
                    </StepCounterWrap>
                    <Typography
                      sx={{
                        fontSize: '13px',
                        fontWeight: 400,
                        lineHeight: '16px',
                        color: 'grey.900'
                      }}
                    >
                      {t(step.labelKey)}
                    </Typography>
                  </StepRow>
                ))}
              </StepperPanel>
            </PreviewBody>
          </PreviewPanel>

          <CalloutRow sx={{ left: '192px', top: '150px' }}>
            <CalloutPill>{t('calloutAddRegions')}</CalloutPill>
            <CalloutArrowIcon />
          </CalloutRow>
          <CalloutRow sx={{ left: '76px', top: '198px' }}>
            <CalloutPill>{t('calloutEditAz')}</CalloutPill>
            <CalloutArrowIcon />
          </CalloutRow>
        </HeroCard>

        <InfoTipBox>
          <TipIconWrap>
            <IdeaIcon />
          </TipIconWrap>
          <Typography
            sx={{
              fontSize: '11.5px',
              fontWeight: 400,
              lineHeight: '16px',
              color: 'grey.700'
            }}
          >
            <Trans
              t={t}
              i18nKey="instanceSettingsTip"
              components={{
                bold: <Box component="span" sx={{ fontWeight: 600 }} />,
                br: <br />
              }}
            />
          </Typography>
        </InfoTipBox>
      </OnboardingModalContent>
    </YBModal>
  );
};
