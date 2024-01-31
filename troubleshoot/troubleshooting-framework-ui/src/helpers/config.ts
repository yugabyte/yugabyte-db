export const IN_DEVELOPMENT_MODE = process.env.NODE_ENV === 'development';

// NOTE: when using REACT_APP_YUGAWARE_API_URL at local development - after login with SSO it will
// set auth cookies for API host domain and redirect to API host root instead of localhost:3000/
// Need to manually set "userId", "customerId" and "PLAY_SESSION" cookies for localhost:3000
export const ROOT_URL =
  process.env.REACT_APP_YUGAWARE_API_URL ??
  (IN_DEVELOPMENT_MODE ? 'http://localhost:8080' : '/api/v1');
