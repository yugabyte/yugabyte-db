import { TFunction } from 'i18next';

export function getCreateUniverseSteps(t: TFunction) {
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
        {
          title: t('nodesAndAvailabilityZone')
        }
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
          title: t('proxySettings')
        },
        {
          title: t('otherAdvancedSettings')
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
