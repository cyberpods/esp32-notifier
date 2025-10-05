# Screenshot Generation

The PNG screenshots for the README can be generated from the HTML files in the parent `docs/` folder.

## Required Screenshots

1. **wifi-setup.png** - From `../wifi-setup.html`
2. **main-config.png** - From `../main-config.html`
3. **logs-viewer.png** - From `../logs-viewer.html`

## How to Generate Screenshots

### Option 1: Using Browser DevTools (Recommended)

1. Open each HTML file in your browser:
   - `file:///path/to/esp32-notifier/docs/wifi-setup.html`
   - `file:///path/to/esp32-notifier/docs/main-config.html`
   - `file:///path/to/esp32-notifier/docs/logs-viewer.html`

2. Press F12 to open DevTools

3. Toggle device toolbar (Ctrl+Shift+M) and set viewport to 800x600

4. Press Ctrl+Shift+P and search for "Capture screenshot"

5. Save to this directory with the appropriate name

### Option 2: Using Python + Selenium

```bash
pip install selenium pillow
# Requires Chrome/Chromium browser
python generate-screenshots.py
```

### Option 3: Using wkhtmltoimage

```bash
sudo apt-get install wkhtmltopdf
wkhtmltoimage --width 800 ../wifi-setup.html wifi-setup.png
wkhtmltoimage --width 800 ../main-config.html main-config.png
wkhtmltoimage --width 1000 ../logs-viewer.html logs-viewer.png
```

### Option 4: Using Playwright (Node.js)

```bash
npm install -g playwright
npx playwright screenshot file://$(pwd)/../wifi-setup.html wifi-setup.png --viewport-size=800,600
npx playwright screenshot file://$(pwd)/../main-config.html main-config.png --viewport-size=800,600
npx playwright screenshot file://$(pwd)/../logs-viewer.html logs-viewer.png --viewport-size=1000,700
```

## Placeholder Images

Until screenshots are generated, placeholder images are provided.
