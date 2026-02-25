import { Box } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBModal } from '../../../../components';
import { PerfAdvisorAPI, QUERY_KEY } from '../../../PerfAdvisor/api';
import { Universe } from '../../universe-form/utils/dto';
import { isNonEmptyArray } from '@app/utils/ObjectUtils';

interface EnablePerfAdvisorModalProps {
  universeData: Universe;
  perfAdvisorStatus: { data: { success: boolean } };
  perfAdvisorDetails: any;
  open: boolean;
  paUuid: string;
  onClose: () => void;
}
export const EnablePerfAdvisorModal = ({
  universeData,
  perfAdvisorStatus,
  perfAdvisorDetails,
  paUuid,
  open,
  onClose
}: EnablePerfAdvisorModalProps) => {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const isUniverseRegisteredToPA =
    perfAdvisorStatus?.data?.success && isNonEmptyArray(perfAdvisorDetails?.data);

  const onSubmit = async () => {
    isUniverseRegisteredToPA
      ? await disablePerfAdvisorToUniverse.mutateAsync()
      : await enablePerfAdvisorToUniverse.mutateAsync();
    onClose();
  };

  // PUT API call to enable Perf Advisdor for the universe
  const enablePerfAdvisorToUniverse = useMutation(
    () => PerfAdvisorAPI.attachUniverseToPerfAdvisor(paUuid, universeData.universeUUID),
    {
      onSuccess: () => {
        toast.success(t('universeActions.paUniverseStatus.enablePaUniverseSuccess'));
        queryClient.invalidateQueries(QUERY_KEY.fetchUniverseRegistrationDetails);
      },
      onError: (e: any) => {
        toast.error(
          `${e?.response?.data?.error}.Check if Yugabyte Anywhere is registered with Perf Advisor Service`
        );
      }
    }
  );

  // DELETE API call to disable Perf Advisdor for the universe
  const disablePerfAdvisorToUniverse = useMutation(
    () => PerfAdvisorAPI.deleteUniverseRegistration(universeData.universeUUID),
    {
      onSuccess: () => {
        toast.success(t('universeActions.paUniverseStatus.disablePaUniverseSuccess'));
        queryClient.invalidateQueries(QUERY_KEY.fetchUniverseRegistrationDetails);
      },
      onError: () => {
        toast.error(t('universeActions.paUniverseStatus.disablePaUniverseFailure'));
      }
    }
  );

  return (
    <YBModal
      open={open}
      title={
        isUniverseRegisteredToPA
          ? t('universeActions.paUniverseStatus.disableTitle')
          : t('universeActions.paUniverseStatus.enableTitle')
      }
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
        <span>
          <Trans
            i18nKey={'universeActions.paUniverseStatus.subText'}
            values={{
              universeName: universeData.name,
              action: isUniverseRegisteredToPA ? 'disable' : 'enable'
            }}
          />
        </span>
      </Box>
    </YBModal>
  );
};
