# Server Setup for LiteOTA

**Kitronic** — [info@kitronic.tech](mailto:info@kitronic.tech) · [www.kitronic.tech](https://www.kitronic.tech)

Place these files on any HTTP server (Apache, Nginx, GitHub Pages, or raw GitHub):

- `project.json` — JSON manifest
- `project.txt`  — plain-text manifest
- `firmware_v1.1.bin` — compiled firmware binary

---

## Generating the `.bin` file

### Arduino IDE
1. Open your sketch
2. `Sketch` → `Export Compiled Binary`
3. Find the `.bin` next to your `.ino` (or inside `build/` on Arduino 2.x)
4. Rename it: `firmware_v1.1.bin`

### PlatformIO
```bash
pio run -e d1_mini
# output: .pio/build/d1_mini/firmware.bin
```

Rename and upload.

---

## Updating

For every new release:

1. Bump `CURRENT_VERSION` in your sketch
2. Export / compile the new binary
3. Upload the new `.bin` to the server
4. Update `project.json` (or `project.txt`) with the new version + URL

---

## GitHub raw URLs

If hosting on GitHub:

1. Commit the `.bin` and the manifest to your repo
2. Use the **raw** URL:

   ```
   https://raw.githubusercontent.com/kitronic/esp-lib-lite-ota/main/liteota/project.json
   ```

⚠️ **Note:** LiteOTA uses `http://` by default. For `https://`, define
`LITEOTA_USE_TLS` in your sketch and enable MFLN to keep RAM low.

---

## Quick local server

```bash
python3 -m http.server 8080
```

Then point your sketch to:

```
http://<your-pc-ip>:8080/project.json
```

---

## Links

- 🐙 [github.com/kitronic/esp-lib-lite-ota](https://github.com/kitronic/esp-lib-lite-ota)
- 📧 [info@kitronic.tech](mailto:info@kitronic.tech)
- 🌐 [www.kitronic.tech](https://www.kitronic.tech)