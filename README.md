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

## ESP32 and Arduino LCD

The **Send detected signs** panel connects to the ESP32 WebSocket server. Enter
the ESP32's local address as `ws://ESP32-IP:81`, then select **Connect**. When
the camera sees a new steady prediction at 55% confidence or higher, the page
sends one `SIGN:<label>` text message. A held sign is not repeatedly sent;
change signs before the same label can be sent again. The URL is saved in that
browser.

The browser and ESP32 must be reachable on the same local network. The existing
ESP32 sketch serves unencrypted `ws://`, so an HTTPS-hosted website cannot
connect directly to it; use the site locally over `http://localhost` for
testing, or place a secure WebSocket proxy on a network reachable by both
devices. Never expose the ESP32 WebSocket port directly to the public internet.

Upload `espthing.ino` to the ESP32 after replacing `YOUR_WIFI_NAME` and
`YOUR_WIFI_PASSWORD` with your Wi-Fi credentials. Set the Serial Monitor to
115200 baud; after the ESP32 connects, it prints both the status-page address
and WebSocket URL. Upload `arduinothing.ino` to an Uno-compatible Arduino.
Install the `LiquidCrystal_I2C` library in Arduino IDE; the sketch assumes a
16x2 LCD backpack at address `0x27`.

To see whether signs are arriving, open the printed HTTP status-page address
in a browser, for example `http://192.168.1.50/`. It displays the last sign
received and the number received. The WebSocket endpoint itself is not a
regular webpage; enter the printed `ws://192.168.1.50:81` address in this
website's **ESP32 WebSocket URL** field and connect. The browser and ESP32 must
be on the same local network.

### Wiring (Uno-compatible Arduino and 16x2 I2C LCD)

| ESP32 / LCD | Arduino |
| --- | --- |
| ESP32 GPIO 17 (UART TX) | A0 (software UART RX) |
| ESP32 GND | Arduino GND |
| LCD SDA | A4 (SDA) |
| LCD SCL | A5 (SCL) |
| LCD VCC | 5V |
| LCD GND | GND |

The UART is one-way, 9600 baud; no Arduino-to-ESP32 TX wire is needed. Connect
the grounds together. Power the Arduino and ESP32 from suitable supplies; do
not connect the ESP32's 3.3V output to the LCD's 5V supply input. The ESP32 TX
is 3.3V logic, which an Uno RX input normally recognizes. If using a different
Arduino board, verify its SoftwareSerial pins and I2C pins before wiring. If
the LCD stays blank, check its contrast adjustment and try the backpack's
actual I2C address in the sketch.

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
