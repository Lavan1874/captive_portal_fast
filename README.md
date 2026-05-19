# captive_portal_fast

Minimal local captive portal detection endpoint for Linux.

It listens on `127.0.0.1:44380` and returns:

- `GET /generate_204` -> `204 No Content`
- `HEAD /generate_204` -> `204 No Content`
- `GET /success.html` -> `200 OK`
- `HEAD /success.html` -> `200 OK` headers only

Build:

```bash
cc -O3 -DNDEBUG -Wall -Wextra -o captive_portal_fast captive_portal_fast.c
strip captive_portal_fast
```

Run:

```bash
./captive_portal_fast
```

Test:

```bash
curl --noproxy '*' -i http://127.0.0.1:44380/generate_204
```

Install latest release binary on a fresh Debian/Ubuntu server:

```bash
curl -fsSL https://raw.githubusercontent.com/Lavan1874/captive_portal_fast/main/install_captive_portal_fast.sh | sudo bash
```
