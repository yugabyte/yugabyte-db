// import type { AllowListChangeOp } from '@app/features/network/allowlist/AllowListSidePanel';

interface ClusterSettingsObject {
  [clusterId: string]: {
    [key: string]: string | number | boolean;
  };
}

// helper that provides single entry point to browser local storage
class BrowserStorage {
  private read(key: string, storage: Storage = localStorage): string {
    return storage.getItem(key) ?? '';
  }

  private write(key: string, value: string, storage: Storage = localStorage) {
    if (value) {
      storage.setItem(key, value);
    } else {
      storage.removeItem(key);
    }
  }

  // track last used account ID for current session only
  get lastUsedAccount(): string {
    return this.read('lastUsedAccount', sessionStorage);
  }
  set lastUsedAccount(value: string) {
    this.write('lastUsedAccount', value, sessionStorage);
  }

  // track last used project ID for current session only
  get lastUsedProject(): string {
    return this.read('lastUsedProject', sessionStorage);
  }
  set lastUsedProject(value: string) {
    this.write('lastUsedProject', value, sessionStorage);
  }

  // track for a short term if a user viewed free tier details spec at the cluster creation wizard
  get viewedFreeTierDetails(): boolean {
    return !!this.read('viewedFreeTierDetails', sessionStorage);
  }
  set viewedFreeTierDetails(value: boolean) {
    this.write('viewedFreeTierDetails', value ? 'yes' : '', sessionStorage);
  }

  // track for a short term if a user viewed paid tier details spec at the cluster creation wizard
  get viewedPaidTierDetails(): boolean {
    return !!this.read('viewedPaidTierDetails', sessionStorage);
  }
  set viewedPaidTierDetails(value: boolean) {
    this.write('viewedPaidTierDetails', value ? 'yes' : '', sessionStorage);
  }

  get authToken(): string {
    return this.read('authToken');
  }
  set authToken(value: string) {
    this.write('authToken', value);
  }

  get language(): string {
    return this.read('language');
  }
  set language(value: string) {
    this.write('language', value);
  }

  get sidebarCollapsed(): boolean {
    return !!this.read('smallSidebar'); // any value means true, no value - false
  }
  set sidebarCollapsed(value: boolean) {
    this.write('smallSidebar', value ? 'yes' : '');
  }

  get announcementAcknowledged(): boolean {
    return !!this.read('announcementDec2021'); // any value means true, no value - false
  }
  set announcementAcknowledged(value: boolean) {
    this.write('announcementDec2021', value ? 'ack' : '');
  }

  get clusterSettings(): ClusterSettingsObject {
    try {
      const result = this.read('clusterSettings');
      return result ? (JSON.parse(result) as ClusterSettingsObject) : {};
    } catch (err) {
      this.write('clusterSettings', '{}');
      return {};
    }
  }
  set clusterSettings(value: ClusterSettingsObject) {
    this.write('clusterSettings', value ? JSON.stringify(value) : '{}');
  }

  // getSessionAllowListOps(item: string): Record<string, AllowListChangeOp> {
  //   try {
  //     const result = window.sessionStorage.getItem(item);
  //     return result ? (JSON.parse(result) as Record<string, AllowListChangeOp>) : {};
  //   } catch (err) {
  //     window.sessionStorage.removeItem(item);
  //     return {};
  //   }
  // }

  deleteSessionAllowListOps(item: string) {
    window.sessionStorage.removeItem(item);
  }

  setSessionAllowListOps(item: string, value: string) {
    window.sessionStorage.setItem(item, value);
  }

  clearCredentials() {
    // clear tokens
    this.authToken = '';
  }

  deleteClusterSettings(id: string) {
    const result = this.read('clusterSettings');
    if (result) {
      try {
        const settings = JSON.parse(result) as ClusterSettingsObject;
        if (settings[id]) {
          delete settings[id];
          this.write('clusterSettings', JSON.stringify(settings));
        }
      } catch (err) {
        // If fails to parse JSON we should clear the key
        this.write('clusterSettings', '{}');
      }
    }
  }
}

export const browserStorage = new BrowserStorage();
