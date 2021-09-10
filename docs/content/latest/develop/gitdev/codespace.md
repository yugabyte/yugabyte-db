---
title: Codespaces
linkTitle: Codespaces
description: GitHub Codespaces integrated dev environment
menu:
  latest:
    identifier: codespaces
    parent: gitdev
    weight: 591
isTocNested: true
showAsideToc: true
---

Use [GitHub Codespaces](https://github.com/features/codespaces) to provision an instant development environment with a pre-configured YugabyteDB.

Codespaces is a configurable cloud development environment accessible via a browser or through a local Visual Studio Code editor. A codespace includes everything developers need to develop for a specific repository, including the Visual Studio Code editing experience, common languages, tools, and utilities. Instantly it sets up a cloud-hosted, containerized, and customizable vscode environment.

Follow the steps below to set up a codespace environment with a pre-configured YugabyteDB. For details on GitHub Codespaces, see the [GitHub Codespaces documentation](https://docs.github.com/en/codespaces).

## Requirements

Codespaces doesn't require anything in your local workstation other than a code editor and Git CLI. Much of the development happens in the cloud through a web browser, though you have the option to use Visual Studio Code locally.

## Getting Started with a boot app

You can find the source at [Spring Boot todo on GitHub](https://github.com/srinivasa-vasu/todo).

The easy way to get started with Codespaces is to simply fork this [source repo](https://github.com/srinivasa-vasu/todo) and jump on to the [Setting up the Codespace environment](#setting-up-the-codespace-environment) section to launch the Codespaces environment for your forked repository. 

If you want to set this up from scratch, then use the following instructions to bootstrap the base project template and copy the appropriate files/content from the [source repo](https://github.com/srinivasa-vasu/todo).

### Initialize the base project structure

Spring todo is a Java Spring Boot reactive app. However, the steps to go through the Codespaces experience are agnostic of the language/framework. A quick way to get started with a spring boot app is via the [Spring Initializer](https://start.spring.io). Generate the base project structure with Webflux, Flyway, and R2DBC dependencies.

![set-up the base project abstract](/images/develop/gitdev/codespace/init-sb.png)

### Complete the CRUD APIs

Complete the todo-service by copying the source and build files from the [source repo]((https://github.com/srinivasa-vasu/todo)) to your repo to handle GET, POST, PUT, and DELETE API requests.

![complete the api endpoints](/images/develop/gitdev/codespace/complete-api.png)

{{< note title="Note" >}}
The application uses non-blocking reactive APIs to connect to YugabyteDB.
{{< /note >}}

## Initialize Codespaces

To get started quickly, you can use one of the appropriate readily available [pre-built containers](https://github.com/microsoft/vscode-dev-containers/tree/main/containers). It can be further customized to fit your needs either by extending them or by creating a new one. A simple click provisions the entire development environment in the cloud with an integrated powerful vscode editor. The entire config to set up the development environment lives in the same source code repository. Let's go through the steps to set up the codespaces environment.

### Set up the Codespace environment

If the Codespaces feature is enabled for your GitHub organization, you can initialize the codespace environment at [GitHub Codespaces](https://github.com/codespaces).
![initalize the codespace environment](/images/develop/gitdev/codespace/init-codespace.png)

If you don't have any codespace-specific specs in the source repo, clicking `Create codespace` creates a default [development environment](https://github.com/microsoft/vscode-dev-containers/tree/main/containers/codespaces-linux) provisioned with a `codespaces-linux` container. This is a universal image with prebuilt language-specific libraries and commonly used utilities; you'll need to customize it to install YugabyteDB. If you like, you can provide your own configuration.

To initialize the codespace environment, open the source code in a local Visual Studio Code editor. Install the following extensions:

- Remote - Containers
- GitHub Codespaces

In the command palette, type `Remote-containers: Add` and select Add Development Container Configuration files. Type `Ubuntu` at the next prompt.

![initialize the remote containers](/images/develop/gitdev/codespace/find-container.png)

This creates a `.devcontainer` folder and a JSON metadata file at the root of the source repository. The `devcontainer.json` file contains provisioning information for the development environment, with the necessary tools and runtime stack. 

### Customize the Codespace environment

You need to customize the default universal image to include the YugabyteDB binary. To do this, you define your own `Dockerfile`. Refer to the [source repo](https://github.com/srinivasa-vasu/todo) for the complete file.

```docker
ARG VERSION
FROM mcr.microsoft.com/vscode/devcontainers/universal:$VERSION

ARG YB_VERSION
ARG ROLE

USER root

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive && \
    apt-get install -y netcat --no-install-recommends

RUN curl -sSLo ./yugabyte.tar.gz https://downloads.yugabyte.com/yugabyte-${YB_VERSION}-linux.tar.gz \
  && mkdir yugabyte \
    && tar -xvf yugabyte.tar.gz -C yugabyte --strip-components=1 \
    && mv ./yugabyte /usr/local/ \
    && ln -s /usr/local/yugabyte/bin/yugabyted /usr/local/bin/yugabyted \
    && ln -s /usr/local/yugabyte/bin/ysqlsh /usr/local/bin/ysqlsh \
    && chmod +x /usr/local/bin/yugabyted \
    && chmod +x /usr/local/bin/ysqlsh \
    && rm ./yugabyte.tar.gz

RUN mkdir -p /var/ybdp \
  && chown -R $ROLE:$ROLE /var/ybdp \
  && chown -R $ROLE:$ROLE /usr/local/yugabyte
```

Update `devcontainer.json` to refer your customized file:

```json
{
  "name": "Yugabyte Codespace",
  "build": {
    "dockerfile": "Dockerfile",
    "args": {
      "VERSION": "focal",
      "YB_VERSION": "2.7.1.1",
      "ROLE": "codespace"
    }
  }
  ...
}
```

The following Docker commands initialize YugabyteDB with an app-specific database:

```docker
RUN echo "CREATE DATABASE todo;" > $STORE/init-db.sql \
  && echo "CREATE USER todo WITH PASSWORD 'todo';" >> $STORE/init-db.sql \
  && echo "GRANT ALL PRIVILEGES ON DATABASE todo TO todo;" >> $STORE/init-db.sql \
  && echo '\\c todo;' >> $STORE/init-db.sql \
  && echo "CREATE EXTENSION IF NOT EXISTS \"uuid-ossp\";" >> $STORE/init-db.sql

RUN echo "/usr/local/yugabyte/bin/post_install.sh 2>&1" >> ~/.bashrc
RUN echo "yugabyted start --base_dir=$STORE/ybd1 --listen=$LISTEN" >> ~/.bashrc
RUN echo "[[ ! -f $STORE/.init-db.sql.completed ]] && " \
  "{ for i in {1..10}; do (nc -vz $LISTEN $PORT >/dev/null 2>&1); [[ \$? -eq 0 ]] && " \
  "{ ysqlsh -f $STORE/init-db.sql; touch $STORE/.init-db.sql.completed; break; } || sleep \$i; done }" >> ~/.bashrc
RUN echo "[[ ! -f $STORE/.init-db.sql.completed ]] && echo 'YugabyteDB is not running!'" >> ~/.bashrc
```

Running the `Create codespace` command with the preceding spec provisions the development environment with a running YugabyteDB instance.

![install YugabyteDB](/images/develop/gitdev/codespace/install-yb.gif)

{{< note title="Note" >}}
GitHub codespaces provisions a fully integrated cloud-native development environment with an automated port forwarding to develop, build, and test applications right from the browser tab.
{{< /note >}}

## Summary

GitHub codespaces provides integrated, pre-configured, and consistent development environments that improve the productivity of distributed teams.

![complete dev environment](/images/develop/gitdev/codespace/complete-dev.png)
