# HMS-Gmail — Multi-stage Docker build
# Produces a self-contained image with C++ runtime + Angular Web UI

# =============================================================================
# Stage 1: Angular UI Builder
# =============================================================================
FROM node:22-slim AS ui-builder

WORKDIR /ui
COPY frontend/package*.json ./
RUN npm ci --no-audit --no-fund
COPY frontend/ ./
RUN npx ng build --configuration production

# =============================================================================
# Stage 2: C++ Builder
# =============================================================================
FROM debian:trixie-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ca-certificates git \
    libcurl4-openssl-dev libssl-dev \
    libpq-dev libpqxx-dev \
    libjsoncpp-dev \
    libpaho-mqtt-dev libpaho-mqttpp-dev \
    nlohmann-json3-dev libspdlog-dev \
    libsqlite3-dev libdrogon-dev \
    uuid-dev libhiredis-dev libbrotli-dev \
    libyaml-cpp-dev zlib1g-dev libfmt-dev \
    libvmime-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY tests/ ./tests/

RUN mkdir build && cd build && \
    cmake -DBUILD_TESTS=OFF -DBUILD_WITH_WEB=ON .. && \
    make -j$(nproc) && \
    strip hms_gmail

# =============================================================================
# Stage 3: Runtime
# =============================================================================
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl \
    libpq5 libpqxx-7 \
    libjsoncpp26 \
    libpaho-mqtt1.3 libpaho-mqttpp3-1 \
    libspdlog1.15 libfmt10 \
    libsqlite3-0 \
    libdrogon1t64 libtrantor1 \
    libyaml-cpp0.8 \
    libvmime1 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -r -u 1000 -m -s /bin/bash gmail

COPY --from=builder /build/build/hms_gmail /usr/local/bin/hms_gmail
RUN chmod +x /usr/local/bin/hms_gmail

COPY --from=ui-builder /ui/dist/frontend/browser/ /home/gmail/static/browser/

RUN mkdir -p /data/gmail /etc/hms-gmail /var/lib/hms-gmail && \
    chown -R gmail:gmail /data /var/lib/hms-gmail /home/gmail/static

USER gmail
WORKDIR /var/lib/hms-gmail

HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
    CMD curl -f http://localhost:${PORT:-8890}/health || exit 1

EXPOSE 8890

ENTRYPOINT ["/usr/local/bin/hms_gmail"]
