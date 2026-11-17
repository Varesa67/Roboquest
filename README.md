# Sign-to-text website files

This static site uses the trained Random Forest in the parent project folder.
It runs hand tracking and sign predictions in the visitor's browser; it does
not record training samples or upload camera images.

## Files to upload

Upload these six files together, preserving their names:

- `index.html`
- `app.js`
- `classifier.mjs`
- `styles.css`
- `sign_classifier.json`
- `hand_landmarker.task`

The site loads MediaPipe's JavaScript/WASM from jsDelivr. Users need internet
access for that library; your sign classifier and hand-landmarker model are
served from the website itself. Camera access requires HTTPS on the hosted
site. To test locally, run a web server from this folder rather than opening
`index.html` as a `file://` URL:

```powershell
py -m http.server 8000
```

Then open `http://localhost:8000` in a supported browser and allow camera
access.

## Export after retraining

The checked-in `sign_classifier.json` is exported from
`..\landmark_classifier.joblib`. If you retrain that model, run this from the
`WebsiteCode` folder to regenerate the JSON before uploading:

```powershell
..\.venv\Scripts\python.exe export_model.py
```

Only upload your own model export. The website model predicts one sign per frame; **Add sign to sentence** commits
a steady prediction to the editable sentence. Single-letter labels append as
letters; `Hello` and `ILY` append as words. The buttons let the visitor add a
space, edit, copy, or clear the sentence.

## Arduino UNO R4 WiFi and LCD

The **Send detected signs** panel connects directly to the Arduino UNO R4
WiFi's WebSocket server. Enter the board's local address as
`ws://UNO-R4-IP:81`, then select **Connect**. When the camera sees a new steady
prediction at 55% confidence or higher, the page sends one `SIGN:<label>` text
message. A held sign is not repeatedly sent; change signs before the same label
can be sent again. The URL is saved in that browser.

The browser and board must be reachable on the same local network. The board
serves unencrypted `ws://`, so an HTTPS-hosted website cannot connect directly
to it. Use the site locally over `http://localhost` for direct connection, or
run the local WSS bridge below on the same computer that opens GitHub Pages.
The bridge forwards messages to the Uno over Wi-Fi; you do not need to buy a
server. Never expose the board's WebSocket port directly to the public internet.

### GitHub Pages local WSS bridge

The bridge listens only on this computer, accepts connections from the
configured GitHub Pages origin, and forwards valid `SIGN:<label>` messages to
the Uno. The board and computer must be on the same Wi-Fi network. This
supports using GitHub Pages in a browser on the bridge computer; it does not
make the Uno accessible to other website visitors.

1. Install Node.js LTS and `mkcert`. On Windows with WinGet, install mkcert
   with `winget install FiloSottile.mkcert`.
2. In PowerShell, create and trust a local certificate:

   ```powershell
   mkcert -install
   $certDir = Join-Path $env:LOCALAPPDATA "uno-wss-bridge"
   New-Item -ItemType Directory -Force $certDir
   mkcert -cert-file (Join-Path $certDir "localhost.pem") `
     -key-file (Join-Path $certDir "localhost-key.pem") localhost 127.0.0.1
   ```

3. From the repository root, install the bridge dependency once:

   ```powershell
   cd .\local-bridge
   npm.cmd install
   ```

4. Start the bridge. Replace the Uno IP address with the one printed by the
   Arduino. For a Pages URL such as
   `https://Varesa67.github.io/Roboquest/`, use the origin
   `https://Varesa67.github.io`:

   ```powershell
   $env:UNO_WS_URL = "ws://192.168.1.50:81"
   $env:GITHUB_PAGES_ORIGIN = "https://Varesa67.github.io"
   $env:TLS_CERT_PATH = Join-Path $env:LOCALAPPDATA "uno-wss-bridge\localhost.pem"
   $env:TLS_KEY_PATH = Join-Path $env:LOCALAPPDATA "uno-wss-bridge\localhost-key.pem"
   npm.cmd start
   ```

5. Copy the full `wss://127.0.0.1:8443/?token=...` URL printed by the bridge
   into the website's **Arduino WebSocket URL** field. Keep the PowerShell
   window open while using the site. The token changes whenever the bridge
   restarts, so enter the newly printed URL after each restart.

If the browser reports a certificate error, ensure `mkcert -install` succeeded
and restart the browser. The existing `http://localhost` method remains
available for using a local copy of the site.

### Upload the sketch

1. In Arduino IDE Boards Manager, install **Arduino UNO R4 Boards**. Install
   the **WebSockets** library by Markus Sattler and a compatible
   **LiquidCrystal_I2C** library.
2. Open `ArduinoUnoR4WiFi.ino`.
3. Replace `YOUR_WIFI_NAME` and `YOUR_WIFI_PASSWORD` in the sketch with your
   Wi-Fi credentials. Do not share a sketch containing your real password.
4. Select **Arduino UNO R4 WiFi** and its connected port, then upload.
5. Open Serial Monitor at **115200 baud**. Once connected, the board prints
   the HTTP status-page address and WebSocket URL.

For a local HTTP copy, enter the printed `ws://...:81` address in the website's
**Arduino WebSocket URL** field. For GitHub Pages, enter the bridge's printed
`wss://...` address instead. Open the printed `http://.../` board address to
view the last sign received and total count.

### LCD wiring (16x2 I2C backpack)

| LCD | Arduino UNO R4 WiFi |
| --- | --- |
| SDA | SDA |
| SCL | SCL |
| VCC | 5V |
| GND | GND |

The sketch assumes an I2C backpack at address `0x27`. If the LCD stays blank,
check its contrast adjustment and try the backpack's actual I2C address in
the sketch. No ESP32, second Arduino, or UART wiring is used in the new setup.
The previous two sketches are retained in the root `archive` folder with
`_archive` suffixes.

Predictions below 55% confidence are shown as **Uncertain** and cannot be
added. Record more varied examples for signs that are repeatedly uncertain or
misidentified, then retrain and export again.

For more reliable predictions, face a light source so your palm is bright,
keep the whole hand inside the camera view, and hold still briefly. The page
checks for hand landmarks about 20 times per second and allows more tolerance
for a temporary tracking-confidence drop when the hand turns.

After replacing `sign_classifier.json` on your host, reload the page. The site
requests the model without using the browser's cached copy. If your host has
its own CDN or server cache, purge that cache or wait for it to expire.

The current export has 26 labels: the static ASL letters A–I and K–Y, plus
`Hello` and `ILY`. J and Z are absent because they require movement, while this
classifier was trained on still hand shapes.

## Model expectations

The browser normalizes landmarks in the same way as the desktop recorder:
mirror the frame, use 21 landmarks in wrist-relative coordinates, scale by the
wrist-to-middle-knuckle distance, and flatten the 63 x/y/z values in landmark
order. `export_model.py` turns each Random Forest tree into JSON for inference
in `classifier.mjs`; this keeps the trained classifier and its classes without
requiring Python on the hosted site.

The current model must have 63 input features. To add labels/classes, collect
examples and retrain with the desktop landmark app, then re-export. The
website itself cannot train.
