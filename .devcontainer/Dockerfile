FROM espressif/idf:v5.3

RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    && rm -rf /var/lib/apt/lists/*

RUN git config --global --add safe.directory "*"

ENV BASH_ENV=/opt/espressif/idf/export.sh