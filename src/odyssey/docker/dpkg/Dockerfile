FROM ubuntu:focal

RUN mkdir /root/odys

WORKDIR /root/odys

COPY . /root/odys

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moskow
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y --no-install-recommends \
    sudo build-essential \
    gcc lsb-release libssl-dev gnupg openssl \
    gdb git \
    libpam0g-dev \
    debhelper debootstrap devscripts make equivs \
    libssl-dev vim valgrind cmake \
    locales; \
    apt-get clean autoclean; \
    apt-get autoremove --yes; \
    rm -rf /var/lib/{apt,dpkg,cache,log}/

ENTRYPOINT ["/root/odys/docker/dpkg/entrypoint.sh"]
