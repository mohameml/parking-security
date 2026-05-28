#!/bin/bash
# Launch dashboard as a lightweight standalone window
# Optimized for Jetson Orin Nano (no GPU acceleration, minimal features)

export DISPLAY=:1
export XAUTHORITY=/home/una/.Xauthority

# Wait for the dashboard to be ready
for i in {1..30}; do
    curl -s -o /dev/null -w "%{http_code}" http://localhost:3000 2>/dev/null | grep -q 200 && break
    sleep 2
done

# Launch Chromium in app mode with performance optimizations
exec /usr/bin/chromium     --app=http://localhost:3000     --window-size=1280,720     --window-position=100,100     --disable-gpu     --disable-software-rasterizer     --disable-features=VaapiVideoDecoder,UseChromeOSDirectVideoDecoder     --disable-dev-shm-usage     --no-first-run     --no-default-browser-check     --disable-pings     --disable-sync     --disable-translate     --disable-background-timer-throttling     --disable-backgrounding-occluded-windows     --disable-renderer-backgrounding     --disable-component-update     --disable-breakpad     --disable-logging     --log-level=3     --user-data-dir=/home/una/.config/parking-dashboard

