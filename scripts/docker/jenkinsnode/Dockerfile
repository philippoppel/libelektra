FROM debian:stretch

# install dependencies
RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install \
  build-essential \
  clang \
  cmake \
  curl \
  dh-lua \
  discount \
  git \
  libaugeas-dev \
  libboost-all-dev \
  libcurl4-gnutls-dev \
  libdbus-1-dev \
  libev-dev \
  libgcrypt20-dev \
  libgit2-dev \
  libglib2.0-dev \
  libgpgme-dev \
  libmarkdown2-dev \
  libpcre++-dev \
  libpcre3-dev \
  libssl-dev \
  libsystemd-dev \
  libuv1-dev \
  libuv1-dev \
  libxerces-c-dev \
  libyajl-dev \
  libzmq3-dev \
  openjdk-8-jdk-headless \
  openssh-server \
  python-all \
  python-dev \
  python3-all \
  ruby-dev \
  ruby-ronn \
  swig \
  unzip \
  valgrind \
  wget \
&& apt-get clean && \
  rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# TODO use Elektra for the configuration steps below

# Google Test
ENV GTEST_ROOT=/opt/gtest
ARG GTEST_VER=release-1.10.0
RUN mkdir -p ${GTEST_ROOT} \
    && cd /tmp \
    && curl -o gtest.tar.gz \
      -L https://github.com/google/googletest/archive/${GTEST_VER}.tar.gz \
    && tar -zxvf gtest.tar.gz --strip-components=1 -C ${GTEST_ROOT} \
    && rm gtest.tar.gz

# setup the ssh server
RUN sed --in-place 's/^\(PermitRootLogin\|UsePAM\|UseDNS\)/#\1/' /etc/ssh/sshd_config && \
  echo "" >> /etc/ssh/sshd_config && \
  echo "# Custom changes from `date`" >> /etc/ssh/sshd_config && \
  echo "PermitRootLogin no" >> /etc/ssh/sshd_config && \
  echo "UsePAM no" >> /etc/ssh/sshd_config && \
  echo "UseDNS no" >> /etc/ssh/sshd_config && \
  echo "SSH daemon config updated"

# setup jenkins prerequisites
RUN echo "\n\n\n\n\nY" | adduser --quiet --disabled-password jenkins && \
  echo "jenkins:<password>" | chpasswd && \
  mkdir /home/jenkins/.m2/ && \
  chown -R jenkins:jenkins /home/jenkins/.m2/ && \
  mkdir /home/jenkins/libelektra && \
  echo "[user]\nname = Jenkins Buildbot\nemail = bot@libelektra.org" >> /home/jenkins/.gitconfig

# setup the run- utilities
COPY run-make /usr/local/bin/run-make
COPY run-make-env /usr/local/bin/run-make-env
COPY run-nice /usr/local/bin/run-nice
RUN chmod a+x /usr/local/bin/run-*

# start the ssh server
EXPOSE 22
RUN service ssh start
CMD ["/usr/sbin/sshd", "-D", "-e"]

# download and install gradle
RUN cd /tmp && wget https://services.gradle.org/distributions/gradle-6.8.3-bin.zip && unzip gradle-6.8.3-bin.zip && rm gradle-6.8.3-bin.zip && mv gradle-6.8.3 /opt/gradle
ENV PATH "${PATH}:/opt/gradle/bin"

USER jenkins:jenkins

# extend PATH for user jenkins to include gradle
RUN echo "export PATH=${PATH}:/opt/gradle/bin" >> /home/jenkins/.bashrc

USER root:root
