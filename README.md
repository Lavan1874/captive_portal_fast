# captive_portal_fast

Minimal local captive portal detection endpoint for Linux. It is designed to
return a successful probe response as quickly as possible on the local host.

It listens on `127.0.0.1:44380` and returns:

- `GET /generate_204` -> `204 No Content`
- `HEAD /generate_204` -> `204 No Content`
- `GET /success.html` -> `200 OK`
- `HEAD /success.html` -> `200 OK` headers only

## Install

Install the latest release binary on a fresh Debian/Ubuntu server:

```bash
curl -fsSL https://raw.githubusercontent.com/Lavan1874/captive_portal_fast/main/install_captive_portal_fast.sh | sudo bash
```

The installer downloads the latest release binary, installs it to:

```text
/usr/local/bin/captive_portal_fast
```

and creates this systemd service:

```text
captive-portal-fast.service
```

The published binary is Linux amd64.

## Build From Source

```bash
cc -O3 -DNDEBUG -Wall -Wextra -o captive_portal_fast captive_portal_fast.c
strip captive_portal_fast
```

Run manually:

```bash
./captive_portal_fast
```

## Service Management

Check service status:

```bash
systemctl status captive-portal-fast --no-pager
```

Check whether it is running:

```bash
systemctl is-active captive-portal-fast
```

Start, stop, and restart:

```bash
systemctl start captive-portal-fast
systemctl stop captive-portal-fast
systemctl restart captive-portal-fast
```

Enable or disable startup on boot:

```bash
systemctl enable captive-portal-fast
systemctl disable captive-portal-fast
```

Disable and stop immediately:

```bash
systemctl disable --now captive-portal-fast
```

Reload systemd after editing the service file:

```bash
systemctl daemon-reload
systemctl restart captive-portal-fast
```

## Logs

Show recent logs:

```bash
journalctl -u captive-portal-fast --no-pager
```

Show the last 50 log lines:

```bash
journalctl -u captive-portal-fast -n 50 --no-pager
```

Follow logs in real time:

```bash
journalctl -u captive-portal-fast -f
```

Show logs since the current boot:

```bash
journalctl -u captive-portal-fast -b --no-pager
```

Show service start failures:

```bash
journalctl -u captive-portal-fast -p warning --no-pager
```

## Manual Tests

Test `/generate_204`:

```bash
curl --noproxy '*' -i http://127.0.0.1:44380/generate_204
```

Expected response:

```text
HTTP/1.1 204 No Content
Connection: close
```

Test `/success.html`:

```bash
curl --noproxy '*' -i http://127.0.0.1:44380/success.html
```

Expected response body:

```html
<html><body>Success</body></html>
```

Test an unknown path:

```bash
curl --noproxy '*' -i http://127.0.0.1:44380/unknown
```

Expected status:

```text
HTTP/1.1 404 Not Found
```

Measure request latency:

```bash
curl --noproxy '*' -o /dev/null -s -w '%{time_total}\n' \
  http://127.0.0.1:44380/generate_204
```

Run 20 latency samples:

```bash
for i in $(seq 1 20); do
  curl --noproxy '*' -o /dev/null -s -w '%{time_total}\n' \
    http://127.0.0.1:44380/generate_204
done
```

## Uninstall

```bash
systemctl disable --now captive-portal-fast
rm -f /etc/systemd/system/captive-portal-fast.service
rm -f /usr/local/bin/captive_portal_fast
systemctl daemon-reload
```
