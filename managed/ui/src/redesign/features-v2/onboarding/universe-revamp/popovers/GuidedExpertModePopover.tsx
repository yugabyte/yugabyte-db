import { FC, MouseEvent, RefObject, useCallback, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTag, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { DEFAULT_RELEASE_NOTES_URL, GradientTitle } from '../modals/HelperComponent';
import MapIcon from '@app/redesign/assets/guided-expert-mode/map-icon.svg';
import CommandIcon from '@app/redesign/assets/guided-expert-mode/command.svg';

const { Box, Link, Popper, Typography, styled } = mui;

/** Vertical gap between the Guided Mode button and the popover. */
const POPOVER_OFFSET: [number, number] = [0, 12];

export const GUIDED_EXPERT_MODE_POPOVER_DISMISS_KEY = 'yb_guided_expert_mode_popover_dismissed';

interface GuidedExpertModePopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  onClose: () => void;
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
  localStorage.getItem(GUIDED_EXPERT_MODE_POPOVER_DISMISS_KEY) === 'true';

export const dismissGuidedExpertModePopover = (): void => {
  localStorage.setItem(GUIDED_EXPERT_MODE_POPOVER_DISMISS_KEY, 'true');
};

export const shouldInterceptGuidedExpertModeClick = (): boolean =>
  !isGuidedExpertModePopoverDismissed();

/**
 * Auto-opens once on create-universe Placement/Regions until dismissed.
 * Only mount this tip when the new experience is already in use.
 */
export const useGuidedExpertModePopover = () => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(!isGuidedExpertModePopoverDismissed());

  const handleGuidedExpertModeClick = useCallback((event: MouseEvent) => {
    if (!shouldInterceptGuidedExpertModeClick()) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    setOpen(true);
  }, []);

  const handleClose = useCallback(() => {
    dismissGuidedExpertModePopover();
    setOpen(false);
  }, []);

  return {
    open,
    anchorRef,
    handleGuidedExpertModeClick,
    handleClose
  };
};

export const GuidedExpertModePopover: FC<GuidedExpertModePopoverProps> = ({
  open,
  anchorRef,
  onClose
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
        <LearnMoreLink href={DEFAULT_RELEASE_NOTES_URL} target="_blank" rel="noopener noreferrer">
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
        <LearnMoreLink href={DEFAULT_RELEASE_NOTES_URL} target="_blank" rel="noopener noreferrer">
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
    <Popper
      open={open}
      anchorEl={guidedButton}
      placement={TourPlacement.BottomEnd}
      modifiers={[
        {
          name: 'offset',
          options: {
            offset: POPOVER_OFFSET
          }
        }
      ]}
      sx={{ zIndex: (theme) => theme.zIndex.modal }}
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
    </Popper>
  );
};
