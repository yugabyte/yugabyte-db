<!--
+++
private = true
+++
-->

Choose the type of YugabyteDB Anywhere installation.

{{< tabpane text=true >}}

  {{% tab header="Docker-based" lang="docker" %}}

For a Docker-based installation, YugabyteDB Anywhere uses [Replicated scheduler](https://www.replicated.com/) for software distribution and container management. You need to ensure that the host can pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).

Replicated installs a compatible Docker version if it is not pre-installed on the host. The currently supported Docker version is 20.10.n.

  {{% /tab %}}

  {{% tab header="Kubernetes-based" lang="kubernetes" %}}

For a Kubernetes-based installation, you need to ensure that the host can pull container images from the [Quay.io](https://quay.io/) container registry. For details, see [Pull and push YugabyteDB Docker images to private container registry](#pull-and-push-yugabytedb-docker-images-to-private-container-registry).

In addition, you need to ensure that core dumps are enabled and configured on the underlying Kubernetes node. For details, see [Specify ulimit and remember the location of core dumps](#specify-ulimit-and-remember-the-location-of-core-dumps).

<!--

A Kubernetes-based installation of YugabyteDB Anywhere requires you to address concerns related to security and core dump collection.

-->

#### Specify ulimit and remember the location of core dumps

The core dump collection in Kubernetes requires special care due to the fact that `core_pattern` is not isolated in cgroup drivers.

You need to ensure that core dumps are enabled on the underlying Kubernetes node. Running the `ulimit -c` command within a Kubernetes pod or node must produce a large non-zero value or the `unlimited` value as an output. For more information, see [How to enable core dumps](https://www.ibm.com/support/pages/how-do-i-enable-core-dumps).

To be able to locate your core dumps, you should be aware of the fact that the location to which core dumps are written depends on the sysctl `kernel.core_pattern` setting. For more information, see [Linux manual: core(5)](https://man7.org/linux/man-pages/man5/core.5.html#:~:text=Naming%20of%20core%20dump%20files).

To inspect the value of the sysctl within a Kubernetes pod or node, execute the following:

```sh
cat /proc/sys/kernel/core_pattern
```

If the value of `core_pattern` contains a `|` pipe symbol (for example, `|/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -- %E`), the core dump is being redirected to a specific collector on the underlying Kubernetes node, with the location depending on the exact collector. To be able to retrieve core dump files in case of a crash within the Kubernetes pod, it is important that you understand where these files are written.

If the value of `core_pattern` is a literal path of the form `/var/tmp/core.%p`, no action is required on your part, as core dumps will be copied by the YugabyteDB node to the persistent volume directory `/mnt/disk0/cores` for future analysis.

Note the following:

- ulimits and sysctl are inherited from Kubernetes nodes and cannot be changed for an individual pod.
- New Kubernetes nodes might be using [systemd-coredump](https://www.freedesktop.org/software/systemd/man/systemd-coredump.html) to manage core dumps on the node.

#### Pull and push YugabyteDB Docker images to private container registry

Due to security concerns, some Kubernetes environments use internal container registries such as  Harbor and Nexus. In this type of setup, YugabyteDB deployment must be able to pull images from and push images to a private registry.

{{< note title="Note" >}}

This is not a recommended approach for enterprise environments. You should ask the container registry administrator to add proxy cache to pull the source images to the internal registry automatically. This would allow you to avoid modifying the Helm chart or providing a custom registry inside the YugabyteDB Anywhere cloud provider.

{{< /note >}}

Before proceeding, ensure that you have the following:

- Pull secret consisting of the user name and password or service account to access source (pull permission) and destination (push and pull permissions) container registries.
- Docker installed on a server (desktop) that can access both container registries. For installation instructions, see [Docker Desktop](https://www.docker.com/products/docker-desktop).

Generally, the process involves the following:

- Fetching the correct version of the YugabyteDB Helm chart whose `values.yaml` file describes all the image locations.
- Retagging images.
- Pushing images to the private container registry.
- Modifying the Helm chart values to point to the new private location.

![img](/images/yp/docker-pull.png)

You need to perform the following steps:

1. Log in to [Quay.io](https://quay.io/) to access the YugabyteDB private registry using the user name and password provided in the secret `yaml` file. To find the `auth` field, use `base64 -d` to decode the data inside the `yaml` file twice. In this field, the user name and password are separated by a colon. For example, `yugabyte+<user-name>:ZQ66Z9C1K6AHD5A9VU28B06Q7N0AXZAQSR`.

   ```sh
   docker login -u “your_yugabyte_username” -p “yugabyte_provided_password” quay.io

   docker search yugabytedb # You should see images
   ```

1. Fetch the YugabyteDB Helm chart on your desktop (install Helm on your desktop). Since the images in the `values.yaml` file may vary depending on the version, you need to specify the version you want to pull and push, as follows:

   ```sh
   helm repo add yugabytedb https://charts.yugabyte.com
   helm repo update
   helm fetch yugabytedb/yugaware - - version= {{ version }}
   tar zxvf yugaware-{{ version }}.tgz
   cd yugaware
   cat values.yaml
   ```

   ```properties
   image:
   commonRegistry: ""
      repository: **quay.io/yugabyte/yugaware**
      tag: **{{ version.build }}**
   pullPolicy: IfNotPresent
   pullSecret: yugabyte-k8s-pull-secret
   thirdparty-deps:
      registry: quay.io
      tag: **latest**
      name: **yugabyte/thirdparty-deps**
   prometheus:
      registry: ""
      tag:  **{{ version.prometheus }}**
      name: **prom/prometheus**
   nginx:
      registry: ""
      tag: **{{ version.nginx }}**
      name: nginx
   ```

1. Pull images to your Docker Desktop, as follows:

   ```sh
   docker pull quay.io/yugabyte/yugaware:{{ version.build }}
   ```

   ```output
   xxxxxxxxx: Pulling from yugabyte/yugaware
   c87736221ed0: Pull complete
   4d33fcf3ee85: Pull complete
   60cbb698a409: Pull complete
   daaf3bdf903e: Pull complete
   eb7b573327ce: Pull complete
   94aa28231788: Pull complete
   16c067af0934: Pull complete
   8ab1e7f695af: Pull complete
   6153ecb58755: Pull complete
   c0f981bfb844: Pull complete
   6485543159a8: Pull complete
   811ba76b1d72: Pull complete
   e325b2ff3e2a: Pull complete
   c351a0ce1ccf: Pull complete
   73765723160d: Pull complete
   588cb609ac0b: Pull complete
   af3ae7e64e48: Pull complete
   17fb23853f77: Pull complete
   cb799d679e2f: Pull complete
   Digest: sha256:0f1cb1fdc1bd4c17699507ffa5a04d3fe5f267049e0675d5d78d77fa632b330c
   Status: Downloaded newer image for quay.io/yugabyte/yugaware:xxxxxx
   quay.io/yugabyte/yugaware:xxxxxxx
   ```

   ```sh
   docker pull quay.io/yugabyte/thirdparty-deps:latest
   ```

   ```output
   latest: Pulling from yugabyte/thirdparty-deps
   c87736221ed0: Already exists
   4d33fcf3ee85: Already exists
   60cbb698a409: Already exists
   d90c5841d133: Pull complete
   8084187ca761: Pull complete
   47e3b9f5c7f5: Pull complete
   64430b56cbd6: Pull complete
   27b03c6bcdda: Pull complete
   ae35ebe6caa1: Pull complete
   9a655eedc488: Pull complete
   Digest: sha256:286a13eb113398e1c4e63066267db4921c7644dac783836515a783cbd25b2c2a
   Status: Downloaded newer image for quay.io/yugabyte/thirdparty-deps:latest
   quay.io/yugabyte/thirdparty-deps:latest
   ```

   ```sh
   docker pull postgres:11.5
   ```

   ```output
   xxxxxx: Pulling from library/postgres
   80369df48736: Pull complete
   b18dd0a6efec: Pull complete
   5c20c5b8227d: Pull complete
   c5a7f905c8ec: Pull complete
   5a3f55930dd8: Pull complete
   ffc097878b09: Pull complete
   3106d02490d4: Pull complete
   88d1fc513b8f: Pull complete
   f7d9cc27056d: Pull complete
   afe180d8d5fd: Pull complete
   b73e04acbb5f: Pull complete
   1dba81bb6cfd: Pull complete
   26bf23ba2b27: Pull complete
   09ead80f0070: Pull complete
   Digest: sha256:b3770d9c4ef11eba1ff5893e28049e98e2b70083e519e0b2bce0a20e7aa832fe
   Status: Downloaded newer image for postgres:11.5
   docker.io/library/postgres:
   ```

   ```sh
   docker pull prom/prometheus:v2.2.1
   ```

   ```output
   Image docker.io/prom/prometheus:v2.2.1 uses outdated schema1 manifest format. Please upgrade to a schema2 image for better future compatibility. More information at https://docs.docker.com/registry/spec/deprecated-schema-v1/
   aab39f0bc16d: Pull complete
   a3ed95caeb02: Pull complete
   2cd9e239cea6: Pull complete
   0266ca3d0dd9: Pull complete
   341681dba10c: Pull complete
   8f6074d68b9e: Pull complete
   2fa612efb95d: Pull complete
   151829c004a9: Pull complete
   75e765061965: Pull complete
   b5a15632e9ab: Pull complete
   Digest: sha256:129e16b08818a47259d972767fd834d84fb70ca11b423cc9976c9bce9b40c58f
   Status: Downloaded newer image for prom/prometheus:
   docker.io/prom/prometheus:
   ```

   ```sh
   docker pull nginx:1.17.4
   ```

   ```output
   1.17.4: Pulling from library/nginx
   8d691f585fa8: Pull complete
   047cb16c0ff6: Pull complete
   b0bbed1a78ca: Pull complete
   Digest: sha256:77ebc94e0cec30b20f9056bac1066b09fbdc049401b71850922c63fc0cc1762e
   Status: Downloaded newer image for nginx:1.17.4
   docker.io/library/nginx:1.17.4
   ```

   ```sh
   docker pull janeczku/go-dnsmasq:release-1.0.7
   ```

   ```output
   release-1.0.7: Pulling from janeczku/go-dnsmasq
   117f30b7ae3d: Pull complete
   504f1e14d6cc: Pull complete
   98e84d0ba41a: Pull complete
   Digest: sha256:3a99ad92353b55e97863812470e4f7403b47180f06845fdd06060773fe04184f
   Status: Downloaded newer image for janeczku/go-dnsmasq:release-1.0.7
   docker.io/janeczku/go-dnsmasq:release-1.0.7
   ```

1. Log in to your target container registry, as per the following example that uses Google Container Registry (GCR) :

   ```sh
   docker login -u _json_key --password-stdin https://gcr.io < .ssh/my-service-account-key.json
   ```

1. Tag the local images to your target registry, as follows:

   ```sh
   docker images
   ```

   ```output
   REPOSITORY              TAG                           IMAGE ID    CREATED     SIZE
   quay.io/yugabyte/yugaware      2.5.1.0-b153               **a04fef023c7c**  6 weeks ago   2.54GB
   quay.io/yugabyte/thirdparty-deps   latest                   **721453480a0f**  2 months ago  447MB
   nginx                1.17.4                          **5a9061639d0a**  15 months ago  126MB
   postgres               11.5                          **5f1485c70c9a**  15 months ago  293MB
   prom/prometheus           v2.2.1                         **cc866859f8df**  2 years ago   113MB
   janeczku/go-dnsmasq         release-1.0.7                      **caef6233eac4**  4 years ago   7.38MB
   ```

   ```sh
   docker tag a04fef023c7c gcr.io/dataengineeringdemos/yugabyte/yugaware:2.5.1.0-b153
   docker tag 721453480a0f gcr.io/dataengineeringdemos/yugabyte/thirdparty-deps:latest
   docker tag 5a9061639d0a gcr.io/dataengineeringdemos/yugabyte/nginx:1.17.4
   docker tag 5f1485c70c9a gcr.io/dataengineeringdemos/yugabyte/postgres:11.5
   docker tag cc866859f8df gcr.io/dataengineeringdemos/prom/prometheus:v2.2.1
   docker tag caef6233eac4 gcr.io/dataengineeringdemos/janeczku/go-dnsmasq:release-1.0.7
   ```

1. Push images to the private container registry, as follows:

   ```sh
   docker push a04fef023c7c
   docker push 721453480a0f
   docker push 5a9061639d0a
   docker push 5f1485c70c9a
   docker push cc866859f8df
   docker push caef6233eac4
   ```

   ![img](/images/yp/docker-image.png)

1. Modify the Helm chart `values.yaml` file. You can map your private internal repository URI to `commonRegistry` and use the folder or `project/image_name` and tags similar to the following:

   ```properties
   image:
      commonRegistry: "**gcr.io/dataengineeringdemos**"
      repository: **“”**
      tag: **2.5.1.0-b153**
      pullPolicy: IfNotPresent
      pullSecret: yugabyte-k8s-pull-secret
      thirdparty-deps:
            registry: /yugabyte/thhirdparty-deps
            tag: **latest**
            name: **yugabyte/thirdparty-deps**
      postgres:
            registry: "yugabyte/postgres"
            tag: 11.5
            name: **postgres**
      prometheus:
            registry: "prom/prometheus"
            tag: **v2.2.1**
            name: **prom/prometheus**
      nginx:
            registry: "yugabyte/nginx"
            tag: **1.17.4**
            name: nginx
      dnsmasq:
            registry: "janeczku/go-dnsmasq/"
            tag: **release-1.0.7**
            name: **janeczku/go-dnsmasq
   ```

1. Install Helm chart or specify the container registry in YugabyteDB Anywhere cloud provider, as follows:

   ```sh
   helm install yugaware **.** -f values.yaml
   ```

  {{% /tab %}}

  {{% tab header="Airgapped" lang="airgapped" %}}

Installing YugabyteDB Anywhere on Airgapped hosts, without access to any Internet traffic (inbound or outbound) requires the following:

- Whitelisting endpoints: to install Replicated and YugabyteDB Anywhere on a host with no Internet connectivity, you have to first download the binaries on a computer that has Internet connectivity, and then copy the files over to the appropriate host. In case of restricted connectivity, the following endpoints have to be whitelisted to ensure that they are accessible from the host marked for installation:
  `https://downloads.yugabyte.com`
  `https://download.docker.com`

- Ensuring that Docker Engine version 20.10.n is available. If it is not installed, you need to follow the procedure described in [Installing Docker in airgapped](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/).
- Ensuring that the following ports are open on the YugabyteDB Anywhere host:
  - 8800 – HTTP access to the Replicated UI
  - 80 or 443 – HTTP and HTTPS access to the YugabyteDB Anywhere UI, respectively
  - 22 – SSH
- Ensuring that the attached disk storage (such as persistent EBS volumes on AWS) is 100 GB minimum.
- Having YugabyteDB Anywhere airgapped install package. Contact Yugabyte Support for more information.
- Signing the Yugabyte license agreement. Contact Yugabyte Support for more information.

  {{% /tab %}}

{{< /tabpane >}}
