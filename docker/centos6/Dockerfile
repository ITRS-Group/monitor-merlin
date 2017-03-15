FROM centos:6
COPY op5repos.repo /etc/yum.repos.d/
COPY RPM-GPG-KEY-op5 /etc/pki/rpm-gpg/

# Good to have
RUN yum install --assumeyes vim yum-utils git
# Building tools
RUN yum install --assumeyes autoconf automake libtool gcc gdb
# Building project
RUN yum install --assumeyes glib2-devel check-devel libdbi-devel op5-naemon-devel gperf
# Running project
RUN yum install --assumeyes naemon monitor-livestatus libdbi-drivers libdbi libdbi-dbd-mysql mysql-libs
# Functional testing
RUN yum install --assumeyes ruby20-devel rubygem20-cucumber rubygem20-multi_json mysql mysql-server mysql-devel rubygem20-op5cucumber
RUN gem2.0 install sequel mysql2
# Update everything
RUN yum upgrade --assumeyes
RUN yum clean all
