import { Box } from '@material-ui/core';
import { useMutation } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBModal } from '../../../../components';
import { PerfAdvisorAPI } from '../../../PerfAdvisor/api';
import { Universe } from '../../universe-form/utils/dto';

interface EnablePerfAdvisorModalProps {
  universeData: Universe;
  perfAdvisorStatus: { data: { success: boolean } };
  open: boolean;
  paUuid: string;
  onClose: () => void;
}
export const EnablePerfAdvisorModal = ({
  universeData,
  perfAdvisorStatus,
  paUuid,
  open,
  onClose
}: EnablePerfAdvisorModalProps) => {
  const { t } = useTranslation();

  const onSubmit = async () => {
    perfAdvisorStatus?.data?.success
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
      },
      onError: (e: any) => {
        toast.error(
          e?.response?.data?.error ?? t('universeActions.paUniverseStatus.enablePaUniverseFailure')
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
        perfAdvisorStatus?.data?.success
          ? t('universeActions.paUniverseStatus.disableTitle')
          : t('universeActions.paUniverseStatus.enableTitle')
      }
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
              action: perfAdvisorStatus?.data?.success ? 'disable' : 'enable'
            }}
          />
        </span>
      </Box>
    </YBModal>
  );
};
