FROM openjdk:8-jre-alpine
MAINTAINER YugaByte
ENV container=yugaware-docker

WORKDIR /opt/yugabyte/yugaware
EXPOSE 9000
ENTRYPOINT ["bin/yugaware", "-Dconfig.file=conf/application.docker.conf"]

RUN apk add --update build-base bash openssl openssl-dev ca-certificates \
  openssh py-pip python-dev libffi-dev &&\
  rm -rf /var/cache/apk/**/

USER root
ADD target/docker/packages/yugaware/yugaware*.tar.gz /opt/yugabyte/yugaware
ADD target/docker/packages/devops/devops*.tar.gz /opt/yugabyte/devops
RUN export USER=root && /opt/yugabyte/devops/bin/install_python_requirements.sh
RUN export USER=root && /opt/yugabyte/devops/bin/install_ansible_requirements.sh
ADD target/docker/packages/yugabyte /opt/yugabyte/releases/
ADD target/docker/packages/java/yb-sample-apps.jar /opt/yugabyte/utils/
ADD target/docker/packages/java/yb-sample-apps-sources.jar /opt/yugabyte/utils/
ADD docker /etc/yugaware

