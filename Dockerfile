FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    libfuse-dev \
    libfuse3-dev \
    fuse3 \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN python3 -m venv .venv && \
    . .venv/bin/activate && \
    pip install fusepy

# Keep container running
CMD ["tail", "-f", "/dev/null"]
