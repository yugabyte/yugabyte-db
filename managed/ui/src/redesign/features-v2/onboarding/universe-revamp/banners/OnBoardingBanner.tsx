import { FC, useCallback, useEffect, useRef, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import {
  AlertVariant,
  mui,
  YBAlert,
  YBPromotionalBanner,
  YBToggle,
  YBTooltip
} from '@yugabyte-ui-library/core';

import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '@app/actions/customers';
import { api, runtimeConfigQueryKey } from '@app/redesign/helpers/api';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import {
  isNewUniverseExperienceForAllUsers,
  isV2CreateEditUniverseEnabled
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseUtils';
import {
  isCurrentUserSuperAdmin,
  isOnboardingNewExperienceEnabled,
  setOnboardingNewExperienceEnabled,
  syncOnboardingNewExperienceEnabled,
  useOnboardingFullscreenOverlayOpen,
  useOnboardingNewExperienceEnabled
} from '../helper-methods';
import { TourStep, dismissTourStep, isTourProgressReady, isTourStepDismissed } from '../tour-progress';
import { DEFAULT_RELEASE_NOTES_URL } from '../modals/HelperComponent';
import { UnsupportedFeatureWarningModal } from '../modals/UnsupportedFeatureWarningModal';
import { WhatChangedModal } from '../modals/WhatChangedModal';
import {
  BeforeNewExperiencePopover,
  isBeforeNewExperiencePopoverDismissed,
  useBeforeNewExperiencePopover
} from '../popovers/BeforeNewExperiencePopover';
import {
  AfterNewExperiencePopover,
  isAfterNewExperiencePopoverDismissed,
  useAfterNewExperiencePopover
} from '../popovers/AfterNewExperiencePopover';
import BoltIcon from '@app/redesign/assets/what-changed/bolt.svg';
import InfoIcon from '@app/redesign/assets/info.svg';

import './OnBoardingBanner.scss';

const { Box, Divider, Link, Typography, styled } = mui;

const ONBOARDING_BANNER_BODY_CLASS = 'onboarding-banner-visible';
/** Delay before auto-opening tip popovers after the banner is shown. */
const TIP_AUTO_OPEN_DELAY_MS = 1400;

const isOnboardingBannerDismissed = (): boolean => isTourStepDismissed(TourStep.Banner);

const dismissOnboardingBanner = (): void => {
  dismissTourStep(TourStep.Banner);
};

const BannerGradientText = styled(Typography)(() => ({
  fontSize: 13,
  fontWeight: 600,
  lineHeight: '16px',
  backgroundImage: 'linear-gradient(-34deg, #EF5824 11%, #ED35C5 40.5%, #7879F1 98.9%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  color: 'transparent'
}));

const BannerRow = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: 32,
  width: '100%',
  flexWrap: 'wrap'
}));

const LeadingGroup = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: 16
}));

const MessageGroup = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: 16,
  flexWrap: 'wrap'
}));

const CompactMessageGroup = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: 8,
  flexWrap: 'wrap'
}));

const ToggleGroup = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: 8
}));

const ToggleLabel = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 500,
  lineHeight: '16px',
  color: theme.palette.grey[600]
}));

const BodyText = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 500,
  lineHeight: '16px',
  color: theme.palette.grey[600]
}));

const SeeWhatsChangedLink = styled(Link)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 500,
  lineHeight: '16px',
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  cursor: 'pointer',
  whiteSpace: 'nowrap',
  '&:hover': {
    color: theme.palette.primary[600],
    textDecoration: 'underline'
  }
}));

const NewBadge = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  gap: 8
}));

const VerticalDivider = styled(Divider)(({ theme }) => ({
  height: 16,
  borderColor: theme.palette.grey[300],
  alignSelf: 'center'
}));

const TooltipContent = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: 8,
  maxWidth: 247,
  padding: 2
}));

const TooltipHeadline = styled(Typography)(() => ({
  fontSize: 11.5,
  fontWeight: 600,
  lineHeight: '16px',
  color: '#4E5F6D'
}));

const TooltipBody = styled(Typography)(() => ({
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '16px',
  color: '#4E5F6D',
  marginTop: 8
}));

const TooltipBold = styled('span')(() => ({
  fontWeight: 700
}));

const TooltipLink = styled(Link)(() => ({
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '16px',
  color: '#2B59C3',
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    color: '#2B59C3',
    textDecoration: 'underline'
  }
}));

const PartyIcon: FC = () => (
  <span role="img" aria-label="celebration" style={{ fontSize: 20, lineHeight: '16px' }}>
    🎉
  </span>
);

export const OnBoardingBanner: FC = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.onBoardingBanner'
  });

  const enabled = useOnboardingNewExperienceEnabled();
  const queryClient = useQueryClient();
  const [showWhatChangedModal, setShowWhatChangedModal] = useState(false);
  const [isUnsupportedFeatureWarningOpen, setUnsupportedFeatureWarningOpen] = useState(false);
  const [isBannerDismissed, setIsBannerDismissed] = useState(isOnboardingBannerDismissed);
  const isFullscreenOverlayOpen = useOnboardingFullscreenOverlayOpen();
  const afterTipTimerRef = useRef<number>();

  const {
    open: isBeforePopoverOpen,
    setOpen: setBeforePopoverOpen,
    anchorRef: seeWhatsChangedAnchorRef,
    handleClose: handleBeforePopoverClose,
    handleClickAway: handleBeforePopoverClickAway
  } = useBeforeNewExperiencePopover();

  const {
    open: isAfterPopoverOpen,
    setOpen: setAfterPopoverOpen,
    handleClose: handleAfterPopoverClose,
    handleClickAway: handleAfterPopoverClickAway
  } = useAfterNewExperiencePopover();

  const clearAfterTipTimer = useCallback(() => {
    if (afterTipTimerRef.current != null) {
      window.clearTimeout(afterTipTimerRef.current);
      afterTipTimerRef.current = undefined;
    }
  }, []);

  const scheduleAfterTipOpen = useCallback(() => {
    clearAfterTipTimer();
    if (isAfterNewExperiencePopoverDismissed()) {
      return;
    }
    setBeforePopoverOpen(false);
    afterTipTimerRef.current = window.setTimeout(() => {
      setAfterPopoverOpen(true);
      afterTipTimerRef.current = undefined;
    }, TIP_AUTO_OPEN_DELAY_MS);
  }, [clearAfterTipTimer, setAfterPopoverOpen, setBeforePopoverOpen]);

  const currentUserInfo = useSelector((state: any) => state.customer.currentUser.data);
  const isSuperAdmin = isCurrentUserSuperAdmin(currentUserInfo?.role);

  // Sync banner dismissed UI when tour progress hydrates from profile.
  useEffect(() => {
    setIsBannerDismissed(isOnboardingBannerDismissed());
  }, [currentUserInfo?.uuid, currentUserInfo?.settings, currentUserInfo?.newUniverseUiTourCompleted]);

  const globalRuntimeConfigQuery = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    api.fetchRuntimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE)
  );

  const isFeatureEnabled =
    globalRuntimeConfigQuery.isSuccess &&
    isV2CreateEditUniverseEnabled(globalRuntimeConfigQuery.data);
  const isEnabledForAll =
    isFeatureEnabled && isNewUniverseExperienceForAllUsers(globalRuntimeConfigQuery.data);

  // Keep in-memory feature mirror aligned with runtime config.
  useEffect(() => {
    if (!globalRuntimeConfigQuery.isSuccess) return;
    syncOnboardingNewExperienceEnabled(isFeatureEnabled);
  }, [globalRuntimeConfigQuery.isSuccess, isFeatureEnabled]);

  // Feature + for-all: dismissable "in use" banner for everyone.
  // Otherwise: opt-in toggle banner for SuperAdmin only.
  // Hide while Edit Placement / Universe Form fullscreen flows are open.
  const isVisible =
    globalRuntimeConfigQuery.isSuccess &&
    !isFullscreenOverlayOpen &&
    (isEnabledForAll ? !isBannerDismissed : isSuperAdmin);

  useEffect(() => {
    if (!isVisible) {
      document.body.classList.remove(ONBOARDING_BANNER_BODY_CLASS);
      return;
    }
    document.body.classList.add(ONBOARDING_BANNER_BODY_CLASS);
    return () => {
      document.body.classList.remove(ONBOARDING_BANNER_BODY_CLASS);
    };
  }, [isVisible]);

  // Enabled for all: After tip after delay.
  useEffect(() => {
    if (
      !isTourProgressReady() ||
      !isVisible ||
      !isEnabledForAll ||
      isAfterNewExperiencePopoverDismissed()
    ) {
      return;
    }
    setBeforePopoverOpen(false);
    const timer = window.setTimeout(() => {
      setAfterPopoverOpen(true);
    }, TIP_AUTO_OPEN_DELAY_MS);
    return () => {
      window.clearTimeout(timer);
    };
  }, [
    isVisible,
    isEnabledForAll,
    setBeforePopoverOpen,
    setAfterPopoverOpen,
    currentUserInfo?.uuid
  ]);

  // Not for-all + SuperAdmin, toggle off: Before tip after delay on load.
  useEffect(() => {
    if (
      !isTourProgressReady() ||
      !isVisible ||
      isEnabledForAll ||
      enabled ||
      isBeforeNewExperiencePopoverDismissed()
    ) {
      return;
    }
    clearAfterTipTimer();
    setAfterPopoverOpen(false);
    const timer = window.setTimeout(() => {
      setBeforePopoverOpen(true);
    }, TIP_AUTO_OPEN_DELAY_MS);
    return () => {
      window.clearTimeout(timer);
    };
  }, [
    isVisible,
    isEnabledForAll,
    enabled,
    clearAfterTipTimer,
    setBeforePopoverOpen,
    setAfterPopoverOpen,
    currentUserInfo?.uuid
  ]);

  // Not for-all + SuperAdmin, toggle already on at first paint: After tip after delay.
  // Toggle flips while mounted are handled only in handleToggle (avoids effect cleanup
  // cancelling the timer when `enabled` updates).
  useEffect(() => {
    if (
      !isTourProgressReady() ||
      !isVisible ||
      isEnabledForAll ||
      !isOnboardingNewExperienceEnabled()
    ) {
      return;
    }
    scheduleAfterTipOpen();
    return clearAfterTipTimer;
  }, [
    isVisible,
    isEnabledForAll,
    scheduleAfterTipOpen,
    clearAfterTipTimer,
    currentUserInfo?.uuid
  ]);

  const invalidateRuntimeConfigs = useCallback(() => {
    void queryClient.invalidateQueries(runtimeConfigQueryKey.ALL);
  }, [queryClient]);

  const handleToggle = useCallback(
    (_event: unknown, checked: boolean) => {
      if (!checked) {
        setUnsupportedFeatureWarningOpen(true);
        return;
      }

      void setOnboardingNewExperienceEnabled(true).then(invalidateRuntimeConfigs);
      toast(
        ({ closeToast }) => (
          <YBAlert
            open
            text={t('switchedToNewExperience')}
            variant={AlertVariant.Success}
            onClose={closeToast}
          />
        ),
        {
          closeButton: false,
          hideProgressBar: true,
          style: { background: 'transparent', boxShadow: 'none', padding: 0 }
        }
      );
      scheduleAfterTipOpen();
    },
    [invalidateRuntimeConfigs, scheduleAfterTipOpen, t]
  );

  const handleSwitchBackConfirm = useCallback(() => {
    setUnsupportedFeatureWarningOpen(false);
    void setOnboardingNewExperienceEnabled(false).then(invalidateRuntimeConfigs);
    clearAfterTipTimer();
    setAfterPopoverOpen(false);
    if (!isBeforeNewExperiencePopoverDismissed()) {
      setBeforePopoverOpen(true);
    }
  }, [
    clearAfterTipTimer,
    invalidateRuntimeConfigs,
    setAfterPopoverOpen,
    setBeforePopoverOpen
  ]);

  const handleSeeWhatsChanged = useCallback(() => {
    setShowWhatChangedModal(true);
  }, []);

  const handleWhatChangedClose = useCallback(() => {
    dismissTourStep(TourStep.WhatChanged);
    setShowWhatChangedModal(false);
  }, []);

  const handleBannerClose = useCallback(() => {
    dismissOnboardingBanner();
    setIsBannerDismissed(true);
  }, []);

  if (!isVisible) {
    return null;
  }

  return (
    <>
      <div className="onboarding-banner-root">
        {isEnabledForAll ? (
          <YBPromotionalBanner
            open
            dismissable={false}
            onClose={handleBannerClose}
            minHeight={48}
            dataTestId="onboarding-banner"
            sx={{
              px: 2,
              py: 1.5,
              boxShadow: '0px 2px 4px 0px rgba(11, 17, 23, 0.1)'
            }}
          >
            <BannerRow sx={{ justifyContent: 'space-between', gap: 2 }}>
              <LeadingGroup>
                <Box
                  component="span"
                  sx={{ display: 'inline-flex', width: 20, height: 20, flexShrink: 0 }}
                >
                  <BoltIcon width={20} height={20} />
                </Box>
                <CompactMessageGroup>
                  <BannerGradientText component="span">
                    {t('usingNewExperienceMessage')}
                  </BannerGradientText>
                  <span ref={seeWhatsChangedAnchorRef}>
                    <SeeWhatsChangedLink
                      component="button"
                      type="button"
                      onClick={handleSeeWhatsChanged}
                      data-testid="onboarding-banner-see-whats-changed"
                    >
                      {t('seeWhatsChanged')}
                    </SeeWhatsChangedLink>
                  </span>
                </CompactMessageGroup>
              </LeadingGroup>
            </BannerRow>
          </YBPromotionalBanner>
        ) : (
          <YBPromotionalBanner
            open
            minHeight={48}
            dismissable={false}
            dataTestId="onboarding-banner"
            sx={{
              px: 2,
              py: 1.5,
              boxShadow: '0px 2px 2px 0px rgba(11, 17, 23, 0.1)'
            }}
          >
            <BannerRow>
              <LeadingGroup>
                {enabled ? (
                  <NewBadge>
                    <BoltIcon width={20} height={20} />
                    <BannerGradientText component="span">{t('newBadge')}</BannerGradientText>
                  </NewBadge>
                ) : (
                  <PartyIcon />
                )}
                <VerticalDivider orientation="vertical" flexItem />
                <MessageGroup>
                  {enabled ? (
                    <BodyText>{t('enabledMessage')}</BodyText>
                  ) : (
                    <BannerGradientText component="span">
                      {t('availableMessage')}
                    </BannerGradientText>
                  )}
                  <span ref={seeWhatsChangedAnchorRef}>
                    <SeeWhatsChangedLink
                      component="button"
                      type="button"
                      onClick={handleSeeWhatsChanged}
                      data-testid="onboarding-banner-see-whats-changed"
                    >
                      {t('seeWhatsChanged')}
                    </SeeWhatsChangedLink>
                  </span>
                </MessageGroup>
              </LeadingGroup>

              <VerticalDivider orientation="vertical" flexItem />

              <ToggleGroup>
                <YBToggle
                  checked={enabled}
                  onChange={handleToggle}
                  dataTestId="onboarding-banner-toggle"
                  labelGap={1}
                  sx={{ marginBottom: '-6px' }}
                />
                <ToggleLabel>{t('tryItFirst')}</ToggleLabel>
                {!enabled ? (
                  <YBTooltip
                    placement="bottom-start"
                    enterDelay={200}
                    leaveDelay={200}
                    PopperProps={{
                      sx: { zIndex: 2200 }
                    }}
                    componentsProps={{
                      tooltip: {
                        sx: {
                          maxWidth: 267,
                          padding: '10px',
                          backgroundColor: '#FFFFFF',
                          color: '#4E5F6D',
                          border: '1px solid #E9EDF0',
                          borderRadius: '8px',
                          boxShadow: '0px 0px 8px 0px rgba(0, 0, 0, 0.1)'
                        }
                      }
                    }}
                    title={
                      <TooltipContent>
                        <Box>
                          <TooltipHeadline>{t('tryItFirstInfoHeadline')}</TooltipHeadline>
                          <TooltipBody>
                            <Trans
                              t={t}
                              i18nKey="tryItFirstInfoBody"
                              values={{
                                runtimeConfig: RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI
                              }}
                              components={{ bold: <TooltipBold /> }}
                            />
                          </TooltipBody>
                        </Box>
                        <TooltipLink
                          href={DEFAULT_RELEASE_NOTES_URL}
                          target="_blank"
                          rel="noopener noreferrer"
                          onClick={(event) => event.stopPropagation()}
                        >
                          {t('findOutMore')}
                        </TooltipLink>
                      </TooltipContent>
                    }
                  >
                    <Box
                      component="span"
                      sx={{ display: 'inline-flex', cursor: 'pointer', color: 'grey.600' }}
                    >
                      <InfoIcon width={16} height={16} />
                    </Box>
                  </YBTooltip>
                ) : (
                  <Box component="span" sx={{ display: 'inline-flex', color: 'grey.600' }}>
                    <InfoIcon width={16} height={16} />
                  </Box>
                )}
              </ToggleGroup>
            </BannerRow>
          </YBPromotionalBanner>
        )}
      </div>

      {!isEnabledForAll && !enabled && (
        <BeforeNewExperiencePopover
          open={isBeforePopoverOpen}
          anchorRef={seeWhatsChangedAnchorRef}
          onClose={handleBeforePopoverClose}
          onClickAway={handleBeforePopoverClickAway}
          onSeeWhatsChanged={handleSeeWhatsChanged}
        />
      )}
      {(isEnabledForAll || enabled) && (
        <AfterNewExperiencePopover
          open={isAfterPopoverOpen}
          anchorRef={seeWhatsChangedAnchorRef}
          onClose={handleAfterPopoverClose}
          onClickAway={handleAfterPopoverClickAway}
          onSeeWhatsChanged={handleSeeWhatsChanged}
        />
      )}
      <WhatChangedModal open={showWhatChangedModal} onClose={handleWhatChangedClose} />
      <UnsupportedFeatureWarningModal
        open={isUnsupportedFeatureWarningOpen}
        onClose={() => setUnsupportedFeatureWarningOpen(false)}
        onSwitchBack={handleSwitchBackConfirm}
      />
    </>
  );
};
