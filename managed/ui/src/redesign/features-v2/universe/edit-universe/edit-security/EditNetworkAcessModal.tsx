import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { mui, yba } from '@yugabyte-ui-library/core';
import { AssignPublicIPField, IPV6Field, NetworkAcessField } from '../../create-universe/fields';
import { useEditUniverse } from '../../../../../v2/api/universe/universe';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/helpers/dtos';
import { isCloudVendorCloudType } from '@app/components/configRedesign/providerRedesign/utils';

const { YBModal } = yba;
const { styled, Box, boxClasses } = mui;

const ModalContent = styled(Box)(({ theme }) => ({
  [`.yb-${boxClasses.root}`]: {
    width: '100%'
  }
}));

interface EditNetworkAcessModalProps {
  open: boolean;
  onClose: () => void;
}

interface NetworkAcessFormProps {
  assignPublicIP?: boolean;
  enableIPV6?: boolean;
  enableExposingService?: boolean;
}

export const EditNetworkAcessModal = ({ open, onClose }: EditNetworkAcessModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.security' });
  const { universeData } = useEditUniverseContext();
  const editUniverse = useEditUniverse();
  const universeUUID = universeData?.info?.universe_uuid;
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const networkingSpec = universeData?.spec?.networking_spec;
  const assignPublicIPValue = !!networkingSpec?.assign_public_ip;
  const ipv6Value = !!networkingSpec?.enable_ipv6;
  const k8sPublicIPValue = Boolean(
    primaryCluster?.networking_spec?.enable_exposing_service === 'EXPOSED'
  );

  const defaultValues =
    providerCode === CloudType.kubernetes
      ? {
          enableIPV6: ipv6Value,
          enableExposingService: k8sPublicIPValue
        }
      : { assignPublicIP: assignPublicIPValue };

  const methods = useForm<NetworkAcessFormProps>({ defaultValues });

  const { handleSubmit } = methods;

  const handleFormSubmit = handleSubmit(async (values) => {
    if (!universeUUID || !primaryCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }
    editUniverse.mutate(
      {
        uniUUID: universeUUID,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: primaryCluster.uuid
            }
          ],
          networking_spec: {
            ...networkingSpec,
            ...(providerCode === CloudType.kubernetes
              ? {
                  enable_ipv6: values.enableIPV6
                }
              : {
                  assign_public_ip: values.assignPublicIP
                })
          }
        }
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('networkAccess')}
      submitLabel={t('apply', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      titleSeparator
      size="md"
      dialogContentProps={{ sx: { padding: '16px !important', gap: '16px' } }}
      overrideHeight={'fit-content'}
      onSubmit={handleFormSubmit}
    >
      <FormProvider {...methods}>
        <ModalContent>
          {isCloudVendorCloudType(providerCode) && (
            <AssignPublicIPField disabled={false} providerCode={providerCode} />
          )}
          {providerCode === CloudType.kubernetes && (
            <>
              <IPV6Field disabled={false} />
              <br />
              <NetworkAcessField disabled={false} />
            </>
          )}
        </ModalContent>
      </FormProvider>
    </YBModal>
  );
};
