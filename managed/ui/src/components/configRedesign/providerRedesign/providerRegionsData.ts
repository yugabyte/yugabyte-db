export const ON_PREM_CUSTOM_LOCATION = 'Custom Location';
export const ON_PREM_LOCATIONS = {
  [ON_PREM_CUSTOM_LOCATION]: { latitude: 0, longitude: 0 },
  Australia: { latitude: -29, longitude: 148 },
  Brazil: { latitude: -22, longitude: -43 },
  China: { latitude: 31.2, longitude: 121.5 },
  'EU East': { latitude: 46, longitude: 25 },
  'EU West': { latitude: 48, longitude: 3 },
  Japan: { latitude: 36, longitude: 139 },
  'New Zealand': { latitude: -43, longitude: 171 },
  'SE Asia': { latitude: 14, longitude: 101 },
  'South Asia': { latitude: 18.4, longitude: 78.4 },
  'US East': { latitude: 36.8, longitude: -79 },
  'US North': { latitude: 48, longitude: -118 },
  'US South': { latitude: 28, longitude: -99 },
  'US West': { latitude: 37, longitude: -121 },
  'EU West - UK': { latitude: 55, longitude: -3 },
  'EU East - Istanbul': { latitude: 41, longitude: 29 },
  'EU Central - Frankfurt': { latitude: 50.1, longitude: 8.7 },
  'EU North - Stockholm': { latitude: 59.3, longitude: 18 },
  'EU South - Milan': { latitude: 45.5, longitude: 9.2 },
  'Middle East - Bahrain': { latitude: 26, longitude: 50.5 },
  'South Africa - Johannesburg': { latitude: -26.2, longitude: 28.04 }
} as const;
