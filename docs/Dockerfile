# This files is publicly available to all contributors
# Please open an issue if you encouter problems

FROM registry.fedoraproject.org/fedora:32

EXPOSE 1313/tcp
RUN yum -y install npm\
	node\
	git\
	hugo\
	&& yum clean all
# volume mount repo to the created dir ( /yugabyte-db)
WORKDIR /yugabyte-db/docs
CMD npm ci && npm start
