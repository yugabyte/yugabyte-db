import { Box, MenuItem, Typography } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { useState } from 'react';
import { useDispatch } from 'react-redux';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBModal, YBRadioGroup, YBSelect } from '../../../../components';
import { PaRegistrationMode, PerfAdvisorAPI, QUERY_KEY } from '../../../PerfAdvisor/api';
import { useListPerfAdvisorEndpoints } from '../../../../../v2/api/perf-advisor-endpoint/perf-advisor-endpoint';
import { Universe } from '../../universe-form/utils/dto';
import { PerfAdvisorModalIntention } from '../../../../../redesign/helpers/constants';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import { showTaskInDrawer } from '../../../../../actions/tasks';
import { createErrorMessage, transitToUniverse } from '../../universe-form/utils/helpers';
import { useIsTaskNewUIEnabled } from '../../../tasks/TaskUtils';

type PerfAdvisorModalIntentionType =
  (typeof PerfAdvisorModalIntention)[keyof typeof PerfAdvisorModalIntention];

interface EnablePerfAdvisorModalProps {
  universeData: Universe;
  perfAdvisorStatus: {
    data: { success?: boolean; advancedObservability?: boolean; mode?: PaRegistrationMode };
  };
  open: boolean;
  paUuid: string;
  onClose: () => void;
  paModalIntention?: PerfAdvisorModalIntentionType;
  isEmbeddedPAEnabled: boolean;
  isPaOnlineModeEnabled?: boolean;
}

export const EnablePerfAdvisorModal = ({
  universeData,
  perfAdvisorStatus,
  paUuid,
  isEmbeddedPAEnabled,
  isPaOnlineModeEnabled = false,
  open,
  onClose,
  paModalIntention = PerfAdvisorModalIntention.ENABLE_OR_DISABLE_PA_COLLECTOR
}: EnablePerfAdvisorModalProps) => {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const dispatch = useDispatch();
  const isNewTaskUIEnabled = useIsTaskNewUIEnabled();
  const isUniverseRegisteredToPA = perfAdvisorStatus?.data?.success;
  const enableAdvancedObservabilityOnly =
    paModalIntention === PerfAdvisorModalIntention.ENABLE_ADVANCED_OBSERVABILITY_ONLY;
  const disableAdvancedObservabilityOnly =
    paModalIntention === PerfAdvisorModalIntention.DISABLE_ADVANCED_OBSERVABILITY_ONLY;

  const [mode, setMode] = useState<PaRegistrationMode>(PaRegistrationMode.BASIC);
  const [paEndpointUuid, setPaEndpointUuid] = useState<string>('');

  // Online mode needs a destination to pick, and there is nothing to offer until one is configured
  // under Integrations -> Perf Advisor -> Endpoints.
  const { data: endpoints = [] } = useListPerfAdvisorEndpoints(undefined, {
    query: { enabled: isPaOnlineModeEnabled && open && !isUniverseRegisteredToPA }
  });
  const canSelectOnline = isPaOnlineModeEnabled && endpoints.length > 0;

  const handleTaskSuccess = (resp: any) => {
    setTimeout(() => {
      dispatch(fetchUniverseInfo(universeData.universeUUID) as any).then((response: any) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }, 2000);
    queryClient.invalidateQueries(QUERY_KEY.fetchUniverseRegistrationDetails);
    if (resp?.taskUUID) {
      if (isNewTaskUIEnabled) {
        dispatch(showTaskInDrawer(resp.taskUUID));
      } else {
        transitToUniverse(universeData.universeUUID);
      }
    }
  };

  const registerUniverse = useMutation(
    (payload: { mode: PaRegistrationMode; paEndpointUuid?: string }) =>
      PerfAdvisorAPI.attachUniverseToPerfAdvisor(
        paUuid,
        universeData.universeUUID,
        payload.mode,
        payload.paEndpointUuid
      ),
    {
      onSuccess: (resp: any) => {
        handleTaskSuccess(resp);
      },
      onError: (e: any) => {
        // Registering in online mode fails here when the universe has not been registered on the
        // destination Perf Advisor yet - that message explains exactly what is missing.
        toast.error(
          createErrorMessage(e) ?? t('universeActions.paUniverseStatus.enablePaUniverseFailure')
        );
      }
    }
  );

  const disablePerfAdvisorToUniverse = useMutation(
    () => PerfAdvisorAPI.deleteUniverseRegistration(universeData.universeUUID),
    {
      onSuccess: (resp: any) => {
        handleTaskSuccess(resp);
      },
      onError: (e: any) => {
        toast.error(
          createErrorMessage(e) ?? t('universeActions.paUniverseStatus.disablePaUniverseFailure')
        );
      }
    }
  );

  const onSubmit = async () => {
    if (enableAdvancedObservabilityOnly) {
      await registerUniverse.mutateAsync({ mode: PaRegistrationMode.ADVANCED });
    } else if (disableAdvancedObservabilityOnly) {
      await registerUniverse.mutateAsync({ mode: PaRegistrationMode.BASIC });
    } else if (isUniverseRegisteredToPA) {
      await disablePerfAdvisorToUniverse.mutateAsync();
    } else {
      await registerUniverse.mutateAsync({
        mode,
        paEndpointUuid: mode === PaRegistrationMode.ONLINE ? paEndpointUuid : undefined
      });
    }
    onClose();
  };

  const title =
    paModalIntention === PerfAdvisorModalIntention.ENABLE_ADVANCED_OBSERVABILITY_ONLY
      ? t('universeActions.paUniverseStatus.enableAdvancedObservability')
      : paModalIntention === PerfAdvisorModalIntention.DISABLE_ADVANCED_OBSERVABILITY_ONLY
        ? t('universeActions.paUniverseStatus.disableAdvancedObservabilityTitle')
        : isUniverseRegisteredToPA
          ? t('universeActions.paUniverseStatus.disableTitle')
          : t('universeActions.paUniverseStatus.enableTitle');

  const modeOptions = [
    {
      value: PaRegistrationMode.BASIC,
      label: t('universeActions.paUniverseStatus.modeBasic'),
      'data-testid': 'EnablePerfAdvisorModal-ModeBasic'
    },
    ...(isEmbeddedPAEnabled
      ? [
          {
            value: PaRegistrationMode.ADVANCED,
            label: t('universeActions.paUniverseStatus.modeAdvanced'),
            'data-testid': 'EnablePerfAdvisorModal-ModeAdvanced'
          }
        ]
      : []),
    ...(canSelectOnline
      ? [
          {
            value: PaRegistrationMode.ONLINE,
            label: t('universeActions.paUniverseStatus.modeOnline'),
            'data-testid': 'EnablePerfAdvisorModal-ModeOnline'
          }
        ]
      : [])
  ];

  const bodyContent = enableAdvancedObservabilityOnly ? (
    <Box component="span" display="block">
      <Trans
        i18nKey="universeActions.paUniverseStatus.enableAdvancedObservabilitySubText"
        values={{ universeName: universeData.name }}
        components={{ strong: <strong /> }}
      />
    </Box>
  ) : disableAdvancedObservabilityOnly ? (
    <Box component="span" display="block">
      <Trans
        i18nKey="universeActions.paUniverseStatus.disableAdvancedObservabilitySubText"
        values={{ universeName: universeData.name }}
        components={{ strong: <strong /> }}
      />
    </Box>
  ) : (
    <>
      <span>
        <Trans
          i18nKey={'universeActions.paUniverseStatus.subText'}
          values={{
            universeName: universeData.name,
            action: isUniverseRegisteredToPA ? 'disable' : 'enable'
          }}
        />
      </span>
      {!isUniverseRegisteredToPA && modeOptions.length > 1 && (
        <Box mt={2}>
          <YBRadioGroup
            label={t('universeActions.paUniverseStatus.modeLabel')}
            options={modeOptions}
            value={mode}
            onChange={(_, value) => setMode(value as PaRegistrationMode)}
          />
        </Box>
      )}
      {!isUniverseRegisteredToPA && mode === PaRegistrationMode.ONLINE && (
        <Box mt={2} display="flex" flexDirection="column" gridGap={8}>
          <Typography variant="body2">
            {t('universeActions.paUniverseStatus.onlineDestinationLabel')}
          </Typography>
          <YBSelect
            value={paEndpointUuid}
            onChange={(event) => setPaEndpointUuid(event.target.value as string)}
            fullWidth
            data-testid="EnablePerfAdvisorModal-Endpoint"
          >
            {endpoints.map((endpoint) => (
              <MenuItem key={endpoint.info?.uuid} value={endpoint.info?.uuid}>
                {endpoint.spec?.name}
              </MenuItem>
            ))}
          </YBSelect>
          <Typography variant="body2" color="textSecondary">
            {t('universeActions.paUniverseStatus.onlinePrerequisite')}
          </Typography>
        </Box>
      )}
    </>
  );

  // Online mode without a destination is rejected by the API, so don't let it be submitted.
  const isSubmitDisabled =
    !isUniverseRegisteredToPA && mode === PaRegistrationMode.ONLINE && !paEndpointUuid;

  return (
    <YBModal
      open={open}
      title={title}
      isSubmitting={registerUniverse.isLoading || disablePerfAdvisorToUniverse.isLoading}
      submitLabel={t('common.applyChanges')}
      cancelLabel={t('common.cancel')}
      buttonProps={{ primary: { disabled: isSubmitDisabled } }}
      onClose={onClose}
      onSubmit={onSubmit}
      overrideWidth="fit-content"
      overrideHeight="fit-content"
      submitTestId="EnablePerfAdvisorModal-Submit"
      cancelTestId="EnablePerfAdvisorModal-Cancel"
    >
      <Box
        display="flex"
        width="100%"
        flexDirection="column"
        pt={2}
        pb={2}
        data-testid="EnablePerfAdvisorModal-Container"
      >
        {bodyContent}
      </Box>
    </YBModal>
  );
};
