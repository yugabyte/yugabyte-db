FROM postgres:${PG_VERSION}-alpine

ENV LANG=C.UTF-8 PGDATA=/pg/data

RUN if [ "${CHECK_CODE}" = "clang" ] ; then \
	# echo 'http://dl-3.alpinelinux.org/alpine/edge/main' > /etc/apk/repositories; \
	# Use alpine/v3.6/main instead of alpine/edge/main to fix version of clang to '8.*.*'
	apk --no-cache add clang-analyzer make musl-dev gcc --repository http://dl-cdn.alpinelinux.org/alpine/v3.6/main; \
	fi

RUN if [ "${CHECK_CODE}" = "cppcheck" ] ; then \
	apk --no-cache add cppcheck --repository http://dl-cdn.alpinelinux.org/alpine/v3.6/community; \
	fi

RUN if [ "${CHECK_CODE}" = "false" ] ; then \
	# echo 'http://dl-3.alpinelinux.org/alpine/edge/main' > /etc/apk/repositories; \
	# Use alpine/v3.6/main instead of alpine/edge/main to fix version of clang to '8.*.*'
	# Install clang as well, since LLVM is enabled in PG_VERSION >= 11 by default
	apk --no-cache add curl python3 gcc make musl-dev llvm clang clang-dev \
		--repository http://dl-cdn.alpinelinux.org/alpine/v3.6/community \
		--repository http://dl-cdn.alpinelinux.org/alpine/v3.6/main; \
	fi

RUN mkdir -p ${PGDATA} && \
	mkdir /pg/src && \
	chown postgres:postgres ${PGDATA} && \
	chmod -R a+rwx /usr/local/lib/postgresql && \
	chmod a+rwx /usr/local/share/postgresql/extension

ADD . /pg/src
WORKDIR /pg/src
RUN chmod -R go+rwX /pg/src
USER postgres
ENTRYPOINT PGDATA=${PGDATA} CHECK_CODE=${CHECK_CODE} bash run_tests.sh
