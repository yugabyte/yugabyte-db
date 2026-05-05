# BYOC API proxy
This is a simple HTTP relay app pulling queued requests and proxying them to a YBA instance.
For use in restricted BYOC deployments where direct calls to YBA from Aeon infra are not possible.
## Use with Aeon API Server
Aeon API server uses a 'service account' whitelist to authenticate internal APIs. 
It expects a JSON file of the following format
```json
[
  {
    "email": "cloud-admin-console-dev@yugabyte.com",
    "password": "random-password-1"
  }
]
```
These creds should be used in the `auth.service_account` section of the app config.  