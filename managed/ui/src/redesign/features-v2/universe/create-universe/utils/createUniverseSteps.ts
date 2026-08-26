import { TFunction } from 'i18next';
import { ResilienceType } from '../steps/resilence-regions/dtos';

export function getCreateUniverseSteps(
  t: TFunction,
  resilienceType?: ResilienceType,
  isK8s = false,
  showOptional = true
) {
  const withOptional = (key: string) =>
    showOptional ? `${t(key)} ${t('optional')}` : t(key);

  return [
    {
      groupTitle: t('general'),
      subSteps: [
        {
          title: t('generalSettings')
        }
      ]
    },
    {
      groupTitle: t('placement'),
      subSteps: [
        {
          title: t('resilienceAndRegions')
        },
        ...(resilienceType === ResilienceType.REGULAR
          ? [
              {
                title: t(isK8s ? 'podsAndAvailabilityZone' : 'nodesAndAvailabilityZone')
              }
            ]
          : [])
      ]
    },
    {
      groupTitle: t('hardware'),
      subSteps: [
        {
          title: t('instanceSettings')
        }
      ]
    },
    {
      groupTitle: t('database'),
      subSteps: [
        {
          title: t('databaseSettings')
        }
      ]
    },
    {
      groupTitle: t('security'),
      subSteps: [
        {
          title: t('securitySettings')
        }
      ]
    },
    {
      groupTitle: t('advanced'),
      subSteps: [
        {
          title: withOptional('proxySettings')
        },
        {
          title: withOptional('otherAdvancedSettings')
        }
      ]
    },
    {
      groupTitle: t('review'),
      subSteps: [
        {
          title: t('summaryAndCost')
        }
      ]
    }
  ];
}
