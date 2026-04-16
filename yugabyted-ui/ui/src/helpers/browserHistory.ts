import { createBrowserHistory } from 'history';

export interface LoginLocationState {
  goBackAfterLogin: boolean;
}

// helper to provide react-router native navigation outside of react components (at Api class in particular)
class BrowserHistory {
  history = createBrowserHistory();
  redirectToLogin(retainPreviousLocation = false) {
    // handle edge case when user already at login screen and history.goBack() won't make sense
    const goBackAfterLogin = this.history.location.pathname === '/login' ? false : retainPreviousLocation;
    this.history.push('/login', { goBackAfterLogin });
  }
  redirectToHome() {
    this.history.push('/');
  }
}

export const browserHistory = new BrowserHistory();
