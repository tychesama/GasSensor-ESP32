# Google Sheets Backend Setup

## What you are building

- Google Sheet acts as the saved backend
- Google Apps Script acts as the API
- ESP32 will send readings into the script
- Website can later read session list and session data from the same script

---

## Step 1. Create the Sheet

Inside Google Apps Script:
- click **New Project** if needed
- rename it to something like `GasSensorBackend`

Then:
- click the **Untitled project** Google Sheet icon if visible, or
- in Apps Script use the bound spreadsheet project if you opened from a Sheet

Simplest route:
1. create a new Google Sheet manually
2. name it something like `GasSensor Data`
3. open **Extensions > Apps Script** from that Sheet
4. paste the script there

That way the script is already bound to the correct sheet.

---

## Step 2. Paste the script

Replace the default `Code.gs` content with the contents of:

- `GoogleSheets-Backend/Code.gs`

Then save.

---

## Step 3. Deploy as Web App

In Apps Script:
- click **Deploy**
- click **New deployment**
- choose **Web app**

Set these:
- **Execute as:** `Me`
- **Who has access:** `Anyone`

Then deploy.

It will ask for authorization. Approve it.

After deployment, copy the **Web app URL**.

It will look something like:

```text
https://script.google.com/macros/s/AKfycbxxxxxxxxxxxxxxxx/exec
```

This URL is the backend endpoint your ESP32 will use.

---

## Step 4. Test in browser

Open the Web app URL in browser.

You should see JSON like:

```json
{
  "ok": true,
  "message": "GasSensor backend online",
  "actions": ["sessions", "readings?session_id=..."]
}
```

Then test:

```text
<YOUR_WEB_APP_URL>?action=sessions
```

At first it should return an empty list.

---

## Step 5. What sheets will be created automatically

The script auto-creates these tabs:

### `sessions`
Columns:
- `session_id`
- `session_name`
- `started_at`
- `ended_at`
- `duration_sec`
- `sample_count`
- `device_label`
- `notes`

### `readings`
Columns:
- `session_id`
- `timestamp`
- `uptime_ms`
- `temperature_c`
- `humidity_pct`
- `gas_adc`

---

## Step 6. How sessions work

- every ESP32 power-on run becomes one session
- session name can just be the date-time string
- each reading belongs to that session
- if ESP32 is off, that creates a gap naturally

This is correct behavior, not a bug.

---

## What comes next after this works

Next I will help you patch the ESP32 so it can:

- generate a `session_id`
- POST readings to this Apps Script URL
- keep the local dashboard
- later fetch saved sessions and readings for display

---

## Quick manual POST test, optional

If you want to test the backend before touching ESP32, use Postman or any REST tool.

POST to your Web app URL with JSON body:

```json
{
  "action": "log_reading",
  "session_id": "2026-04-19_17-00-00",
  "session_name": "2026-04-19 17:00:00",
  "timestamp": "2026-04-19 17:00:02",
  "uptime_ms": 2000,
  "temperature_c": 29.4,
  "humidity_pct": 61,
  "gas_adc": 540,
  "device_label": "ESP32 Gas Monitor"
}
```

If it works, rows should appear in:
- `sessions`
- `readings`

---

## Important practical note

Google Apps Script is fine for school project scale.
Do not treat it like a high-throughput production API.
For your use case, it is good enough.
