import { AxiosError } from 'axios';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { mui, yba } from '@yugabyte-ui-library/core';

import { handleServerError } from '@app/utils/errorHandlingUtils';
import { SupportBundle } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { useDeleteSupportBundleV2 } from '../hooks/useSupportBundles';
import { getSupportBundleName } from '../model/bundleDisplay';

const { Typography } = mui;
const { YBModal } = yba;

interface DeleteSupportBundleModalProps {
  universeUuid: string;
  /** The modal is open while a bundle is set. */
  bundle: SupportBundle | null;
  onClose: () => void;
}

export const DeleteSupportBundleModal = ({
  universeUuid,
  bundle,
  onClose
}: DeleteSupportBundleModalProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.supportBundleV2.deleteModal'
  });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });
  const deleteBundle = useDeleteSupportBundleV2(universeUuid);

  const onSubmit = () => {
    const bundleUuid = bundle?.info?.uuid;
    if (!bundleUuid) {
      return;
    }
    deleteBundle.mutate(bundleUuid, {
      onSuccess: () => {
        toast.success(t('success'));
        onClose();
      },
      onError: (error) => handleServerError(error as AxiosError, { customErrorLabel: t('failed') })
    });
  };

  return (
    <YBModal
      open={!!bundle}
      onClose={onClose}
      onSubmit={onSubmit}
      title={t('title')}
      submitLabel={t('submit')}
      cancelLabel={tCommon('cancel')}
      size="xs"
      overrideHeight="fit-content"
      titleSeparator
      buttonProps={{
        primary: { disabled: deleteBundle.isLoading, dataTestId: 'DeleteSupportBundleModal-Submit' }
      }}
      submitTestId="DeleteSupportBundleModal-Submit"
      cancelTestId="DeleteSupportBundleModal-Cancel"
    >
      <Typography variant="body2" padding={1}>
        {bundle ? t('body', { bundleName: getSupportBundleName(bundle) }) : null}
      </Typography>
    </YBModal>
  );
};
