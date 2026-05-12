import { Box } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { useState } from 'react';
import { useDispatch } from 'react-redux';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBCheckbox, YBModal } from '../../../../components';
import { PerfAdvisorAPI, QUERY_KEY } from '../../../PerfAdvisor/api';
import { Universe } from '../../universe-form/utils/dto';
import { PerfAdvisorModalIntention } from '../../../../../redesign/helpers/constants';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import { showTaskInDrawer } from '../../../../../actions/tasks';
import { transitToUniverse } from '../../universe-form/utils/helpers';
import { useIsTaskNewUIEnabled } from '../../../tasks/TaskUtils';

type PerfAdvisorModalIntentionType =
  (typeof PerfAdvisorModalIntention)[keyof typeof PerfAdvisorModalIntention];

interface EnablePerfAdvisorModalProps {
  universeData: Universe;
  perfAdvisorStatus: { data: { success?: boolean; advancedObservability?: boolean } };
  open: boolean;
  paUuid: string;
  onClose: () => void;
  paModalIntention?: PerfAdvisorModalIntentionType;
}
export const EnablePerfAdvisorModal = ({
  universeData,
  perfAdvisorStatus,
  paUuid,
  open,
  onClose,
  paModalIntention = PerfAdvisorModalIntention.ENABLE_OR_DISABLE_PA_COLLECTOR
}: EnablePerfAdvisorModalProps) => {
  const { t } = useTranslation();
  const [advancedObservability, setAdvancedObservability] = useState(false);
  const queryClient = useQueryClient();
  const dispatch = useDispatch();
  const isNewTaskUIEnabled = useIsTaskNewUIEnabled();
  const isUniverseRegisteredToPA = perfAdvisorStatus?.data?.success;
  const enableAdvancedObservabilityOnly =
    paModalIntention === PerfAdvisorModalIntention.ENABLE_ADVANCED_OBSERVABILITY_ONLY;
  const disableAdvancedObservabilityOnly =
    paModalIntention === PerfAdvisorModalIntention.DISABLE_ADVANCED_OBSERVABILITY_ONLY;

  const onSubmit = async () => {
    if (enableAdvancedObservabilityOnly) {
      await enableAdvancedObservabilityOnlyStatus.mutateAsync();
    } else if (disableAdvancedObservabilityOnly) {
      await disableAdvancedObservabilityOnlyStatus.mutateAsync();
    } else if (isUniverseRegisteredToPA) {
      await disablePerfAdvisorToUniverse.mutateAsync();
    } else {
      await enablePerfAdvisorToUniverse.mutateAsync();
    }
    onClose();
  };

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

  const enableAdvancedObservabilityOnlyStatus = useMutation(
    () =>
      PerfAdvisorAPI.attachUniverseToPerfAdvisor(
        paUuid,
        universeData.universeUUID,
        true
      ),
    {
      onSuccess: (resp: any) => {
        handleTaskSuccess(resp);
      },
      onError: (e: any) => {
        toast.error(
          e?.response?.data?.error ??
            t('universeActions.paUniverseStatus.enableAdvancedObservabilityFailure')
        );
      }
    }
  );

  const disableAdvancedObservabilityOnlyStatus = useMutation(
    () =>
      PerfAdvisorAPI.attachUniverseToPerfAdvisor(
        paUuid,
        universeData.universeUUID,
        false
      ),
    {
      onSuccess: (resp: any) => {
        handleTaskSuccess(resp);
      },
      onError: (e: any) => {
        toast.error(
          e?.response?.data?.error ??
            t('universeActions.paUniverseStatus.disableAdvancedObservabilityFailure')
        );
      }
    }
  );

  const enablePerfAdvisorToUniverse = useMutation(
    () =>
      PerfAdvisorAPI.attachUniverseToPerfAdvisor(
        paUuid,
        universeData.universeUUID,
        advancedObservability
      ),
    {
      onSuccess: (resp: any) => {
        handleTaskSuccess(resp);
      },
      onError: (e: any) => {
        toast.error(
          `${e?.response?.data?.error}.Check if Yugabyte Anywhere is registered with Perf Advisor Service`
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
      onError: () => {
        toast.error(t('universeActions.paUniverseStatus.disablePaUniverseFailure'));
      }
    }
  );

  const title =
    paModalIntention === PerfAdvisorModalIntention.ENABLE_ADVANCED_OBSERVABILITY_ONLY
      ? t('universeActions.paUniverseStatus.enableAdvancedObservability')
      : paModalIntention === PerfAdvisorModalIntention.DISABLE_ADVANCED_OBSERVABILITY_ONLY
        ? t('universeActions.paUniverseStatus.disableAdvancedObservabilityTitle')
        : isUniverseRegisteredToPA
          ? t('universeActions.paUniverseStatus.disableTitle')
          : t('universeActions.paUniverseStatus.enableTitle');

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
      {!isUniverseRegisteredToPA && (
        <Box mt={2}>
          <YBCheckbox
            checked={advancedObservability}
            onChange={(e) => setAdvancedObservability(e.target.checked)}
            label={t('universeActions.paUniverseStatus.enableAdvancedObservability')}
            inputProps={{
              'data-testid': 'EnablePerfAdvisorModal-AdvancedObservability'
            }}
          />
        </Box>
      )}
    </>
  );

  return (
    <YBModal
      open={open}
      title={title}
      isSubmitting={enablePerfAdvisorToUniverse.isLoading || disablePerfAdvisorToUniverse.isLoading}
      submitLabel={t('common.applyChanges')}
      cancelLabel={t('common.cancel')}
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
