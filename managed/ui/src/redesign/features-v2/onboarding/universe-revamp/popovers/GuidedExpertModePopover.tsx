import { FC, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTag, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { DEFAULT_RELEASE_NOTES_URL, GradientTitle } from '../modals/HelperComponent';
import {
  TourStep,
  dismissTourStep,
  isTourStepDismissed,
  subscribeTourProgressReady
} from '../tour-progress';
import MapIcon from '@app/redesign/assets/guided-expert-mode/map-icon.svg';
import CommandIcon from '@app/redesign/assets/guided-expert-mode/command.svg';
import { OnboardingTourPopper } from './OnboardingTourPopper';

const { Box, Link, Typography, styled } = mui;

/** Vertical gap between the Guided Mode button and the popover. */
const POPOVER_OFFSET: [number, number] = [0, 12];

interface GuidedExpertModePopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  /** Permanent Hide Tip. */
  onClose: () => void;
  /** Transient click-away close. */
  onClickAway: () => void;
}

const WideSpotlight = styled(YBTourSpotlight)(() => ({
  '&&': {
    width: 738,
    minHeight: 'unset',
    maxWidth: 'calc(100vw - 32px)',
    padding: '24px 16px 16px'
  }
}));

const Columns = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'flex-start',
  justifyContent: 'space-between',
  gap: '12px',
  width: '100%',
  marginTop: 0
}));

const ModeColumn = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  width: 347,
  maxWidth: '100%',
  padding: '0 16px 8px',
  boxSizing: 'border-box'
}));

const ModeHeader = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  alignItems: 'flex-start',
  justifyContent: 'center',
  width: '100%'
}));

const ModeTitleRow = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: '8px'
}));

const ModeCopy = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '8px',
  width: '100%'
}));

const Subtitle = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 600,
  lineHeight: '18px',
  color: theme.palette.grey[600]
}));

const Description = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 400,
  lineHeight: '18px',
  color: theme.palette.grey[700]
}));

const ExpertTitle = styled(Typography)(({ theme }) => ({
  fontSize: 15,
  fontWeight: 600,
  lineHeight: '16px',
  color: theme.palette.grey[700]
}));

const LearnMoreLink = styled(Link)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 400,
  lineHeight: '18px',
  color: theme.palette.grey[700],
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    color: theme.palette.grey[700],
    textDecoration: 'underline'
  }
}));

export const isGuidedExpertModePopoverDismissed = (): boolean =>
  isTourStepDismissed(TourStep.GuidedExpert);

export const dismissGuidedExpertModePopover = (): void => {
  dismissTourStep(TourStep.GuidedExpert);
};

/** Delay before auto-opening the Guided/Expert tip on create-universe. */
const AUTO_OPEN_DELAY_MS = 700;

/**
 * Auto-opens once on create-universe Placement/Regions until dismissed.
 * Only mount this tip when the new experience is already in use.
 */
export const useGuidedExpertModePopover = () => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    let timer: number | undefined;
    const unsub = subscribeTourProgressReady(() => {
      if (isGuidedExpertModePopoverDismissed()) {
        return;
      }
      timer = window.setTimeout(() => {
        setOpen(true);
      }, AUTO_OPEN_DELAY_MS);
    });
    return () => {
      unsub();
      if (timer != null) window.clearTimeout(timer);
    };
  }, []);

  const handleClose = useCallback(() => {
    dismissGuidedExpertModePopover();
    setOpen(false);
  }, []);

  const handleOpen = useCallback(() => {
    setOpen(true);
  }, []);

  return {
    open,
    anchorRef,
    handleOpen,
    handleClose,
    handleClickAway: handleClose
  };
};

export const GuidedExpertModePopover: FC<GuidedExpertModePopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onClickAway
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.guidedExpertModePopover'
  });

  const body = (
    <Columns>
      <ModeColumn>
        <ModeHeader>
          <YBTag size="small" variant="light" color="purple">
            {t('guided.badge')}
          </YBTag>
          <ModeTitleRow>
            <MapIcon width={24} height={24} />
            <GradientTitle component="span">{t('guided.title')}</GradientTitle>
          </ModeTitleRow>
        </ModeHeader>
        <ModeCopy>
          <Subtitle>{t('guided.subtitle')}</Subtitle>
          <Description>{t('guided.description')}</Description>
        </ModeCopy>
        <LearnMoreLink
          href={
            'https://deploy-preview-33264--infallible-bardeen-164bc9.netlify.app/stable/yugabyte-platform/create-deployments/create-universes-overview/#guided-mode'
          }
          target="_blank"
          rel="noopener noreferrer"
        >
          {t('learnMore')}
        </LearnMoreLink>
      </ModeColumn>
      <ModeColumn>
        <ModeHeader>
          <YBTag size="small" variant="light">
            {t('expert.badge')}
          </YBTag>
          <ModeTitleRow>
            <CommandIcon width={24} height={24} />
            <ExpertTitle component="span">{t('expert.title')}</ExpertTitle>
          </ModeTitleRow>
        </ModeHeader>
        <ModeCopy>
          <Subtitle>{t('expert.subtitle')}</Subtitle>
          <Description>{t('expert.description')}</Description>
        </ModeCopy>
        <LearnMoreLink
          href={
            'https://deploy-preview-33264--infallible-bardeen-164bc9.netlify.app/stable/yugabyte-platform/create-deployments/create-universes-overview/#expert-mode'
          }
          target="_blank"
          rel="noopener noreferrer"
        >
          {t('learnMore')}
        </LearnMoreLink>
      </ModeColumn>
    </Columns>
  );

  // Anchor to the Guided Mode button so the arrow points at it, not the Expert button.
  const guidedButton =
    (anchorRef.current?.querySelector(
      '[data-testid="guided-mode-button"]'
    ) as HTMLElement | null) ?? anchorRef.current;

  return (
    <OnboardingTourPopper
      open={open}
      anchorEl={guidedButton}
      placement={TourPlacement.BottomEnd}
      offset={POPOVER_OFFSET}
      zIndex={(theme) => theme.zIndex.modal}
      onClickAway={onClickAway}
    >
      <WideSpotlight
        title=""
        body={body}
        badgeLabel=""
        showNext={false}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.BottomEnd}
        dataTestId="guided-expert-mode-popover-spotlight"
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};
