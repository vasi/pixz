FROM ubuntu:noble

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
  && apt-get -y install --no-install-recommends git
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
RUN apt-get -y install --no-install-recommends \
  build-essential \
  autoconf automake pkg-config \
  asciidoc cppcheck docbook-xml docbook-xsl \
  libarchive-dev liblzma-dev
