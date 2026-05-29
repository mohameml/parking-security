# Deployment

## First-Time Production Install on a Jetson

### 1. Flash JetPack 6.2+

Follow NVIDIA's instructions to flash the Jetson with JetPack 6.2 or newer:
- https://developer.nvidia.com/embedded/jetpack

Enable **MAXN Super** power mode (or 25W) for production:

```bash
sudo nvpmodel -m 2   # MAXN_SUPER on Jetson Orin Nano Super
sudo jetson_clocks
```

Add to `/etc/systemd/system/jetson-clocks.service` to persist across reboots.

### 2. Install PostgreSQL

```bash
sudo apt install postgresql postgresql-contrib
sudo -u postgres createdb parking_security
sudo -u postgres psql -c "CREATE USER parking WITH PASSWORD 'CHANGE_ME';"
sudo -u postgres psql -c "GRANT ALL ON DATABASE parking_security TO parking;"

# Apply schema
psql -U parking -d parking_security -f config/schema.sql
```

### 3. Build the Project

```bash
git clone <repo> ~/parking-security
cd ~/parking-security
./scripts/setup_jetson.sh
./scripts/convert_models.sh ~/buffalo_l   # After copying buffalo_l ONNX files
./scripts/build.sh
```

### 4. Install as a systemd Service

Create `/etc/systemd/system/parking-security.service`:

```ini
[Unit]
Description=Parking Security — face recognition access control
After=network.target postgresql.service
Requires=postgresql.service

[Service]
Type=simple
User=parking
WorkingDirectory=/opt/parking-security
Environment=PARKING_DB_URL=host=localhost user=parking password=... dbname=parking_security
Environment=PARKING_LOG_LEVEL=info
ExecStart=/opt/parking-security/bin/parking-security /opt/parking-security/config/parking.ini
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Then:

```bash
sudo cmake --install build/release --prefix /opt/parking-security
sudo useradd -r -s /sbin/nologin parking
sudo chown -R parking:parking /opt/parking-security
sudo systemctl enable --now parking-security
```

### 5. Configure Alerts (Optional)

Edit `/opt/parking-security/config/parking.ini`:

```ini
[alerts]
telegram_bot_token = <from @BotFather>
telegram_chat_id   = <your chat id>
smtp_user          = you@gmail.com
smtp_password      = <app password>
alert_email_to     = security@example.com
```

Restart: `sudo systemctl restart parking-security`.

### 6. Enroll People

Either copy data from the Python project or use the CLI:

```bash
# Single enrollment
./build/release/tools/enroll_face student B020785 "Ahmed B." ~/images/B020785.jpg

# Batch — iterate the images folder
for photo in ~/images/*.jpg; do
    id=$(basename "$photo" .jpg)
    ./build/release/tools/enroll_face student "$id" "$id" "$photo"
done
```

For schedule import from the Excel, see `tools/import_schedule.cpp` (to be
written during implementation phase — or use the Python project's importer
since both share the database schema).

## Operations

### Logs

```bash
journalctl -u parking-security -f         # Live tail
journalctl -u parking-security --since today
```

### Live Status

```bash
systemctl status parking-security
```

The app also exposes metrics via the optional HTTP endpoint (if `api.enabled=true`):

```bash
curl http://localhost:8000/api/health
curl http://localhost:8000/api/stats
```

### Backup

```bash
# Nightly DB backup — add to crontab
pg_dump -U parking parking_security | gzip > /backup/parking-$(date +%F).sql.gz
```

### Upgrade

```bash
cd ~/parking-security
git pull
./scripts/build.sh
sudo cmake --install build/release --prefix /opt/parking-security
sudo systemctl restart parking-security
```

### Monitoring

systemd's `Restart=always` handles crash recovery. For higher visibility
(e.g., alert if the service is down > 60s), integrate with Prometheus
`node_exporter` or Nagios.

## Network Ports

| Port | Purpose                  | Access    |
|------|--------------------------|-----------|
| 5432 | PostgreSQL               | localhost |
| 8000 | HTTP API (optional)      | LAN       |
| 8080 | MJPEG stream (optional)  | LAN       |

Block all others at the firewall:

```bash
sudo ufw allow from 192.168.100.0/24 to any port 8000
sudo ufw allow from 192.168.100.0/24 to any port 8080
sudo ufw enable
```

## Security Hardening

- Change default DB password
- Generate a unique JWT signing key if HTTP API is enabled
- Keep PostgreSQL bound to localhost (default)
- Run the service as a non-root user (`parking`)
- Enable UFW, allow only SSH + dashboard ports
- For HTTPS on the HTTP API, terminate TLS at nginx or Caddy
