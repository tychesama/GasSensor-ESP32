function doGet(e) {
  const action = (e && e.parameter && e.parameter.action) || '';

  if (action === 'sessions') {
    return jsonOut(getSessions());
  }

  if (action === 'readings') {
    const sessionId = (e && e.parameter && e.parameter.session_id) || '';
    return jsonOut(getReadings(sessionId));
  }

  return jsonOut({
    ok: true,
    message: 'GasSensor backend online',
    actions: ['sessions', 'readings?session_id=...']
  });
}

function doPost(e) {
  try {
    const body = e && e.postData && e.postData.contents
      ? JSON.parse(e.postData.contents)
      : {};
    const params = (e && e.parameter) || {};

    const action = body.action || params.action || 'log_reading';
    const merged = Object.assign({}, params, body);

    if (action === 'log_reading') {
      return jsonOut(logReading(merged));
    }

    if (action === 'delete_session') {
      return jsonOut(deleteSession(merged));
    }

    return jsonOut({ ok: false, error: 'Unknown action' });
  } catch (err) {
    return jsonOut({ ok: false, error: String(err) });
  }
}

function jsonOut(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function getSheet(name) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sh = ss.getSheetByName(name);
  if (!sh) sh = ss.insertSheet(name);
  return sh;
}

function ensureSheets() {
  const sessions = getSheet('sessions');
  const readings = getSheet('readings');

  if (sessions.getLastRow() === 0) {
    sessions.appendRow([
      'session_id',
      'session_name',
      'started_at',
      'ended_at',
      'duration_sec',
      'sample_count',
      'device_label',
      'notes'
    ]);
  }

  if (readings.getLastRow() === 0) {
    readings.appendRow([
      'session_id',
      'timestamp',
      'uptime_ms',
      'temperature_c',
      'humidity_pct',
      'gas_adc'
    ]);
  }
}

function logReading(body) {
  ensureSheets();

  const sessions = getSheet('sessions');
  const readings = getSheet('readings');

  const sessionId = String(body.session_id || '').trim();
  if (!sessionId) {
    return { ok: false, error: 'session_id is required' };
  }

  const timestamp = body.timestamp || new Date().toISOString();
  const uptimeMs = Number(body.uptime_ms || 0);
  const temp = Number(body.temperature_c || 0);
  const hum = Number(body.humidity_pct || 0);
  const gas = Number(body.gas_adc || 0);
  const deviceLabel = String(body.device_label || 'ESP32 Gas Monitor');

  readings.appendRow([sessionId, timestamp, uptimeMs, temp, hum, gas]);

  const sessionData = upsertSession_(sessions, {
    sessionId: sessionId,
    sessionName: body.session_name || sessionId,
    timestamp: timestamp,
    uptimeMs: uptimeMs,
    deviceLabel: deviceLabel
  });

  return {
    ok: true,
    session_id: sessionId,
    session: sessionData
  };
}

function upsertSession_(sheet, data) {
  const values = sheet.getDataRange().getValues();
  const targetId = data.sessionId;

  for (let i = 1; i < values.length; i++) {
    if (String(values[i][0]) === targetId) {
      const startedAt = values[i][2] || data.timestamp;
      const endedAt = data.timestamp;
      const durationSec = Math.max(0, Math.round((Number(data.uptimeMs) || 0) / 1000));
      const sampleCount = Number(values[i][5] || 0) + 1;

      sheet.getRange(i + 1, 2, 1, 7).setValues([[
        values[i][1] || data.sessionName,
        startedAt,
        endedAt,
        durationSec,
        sampleCount,
        values[i][6] || data.deviceLabel,
        values[i][7] || ''
      ]]);

      return {
        session_id: targetId,
        started_at: startedAt,
        ended_at: endedAt,
        duration_sec: durationSec,
        sample_count: sampleCount
      };
    }
  }

  sheet.appendRow([
    targetId,
    data.sessionName,
    data.timestamp,
    data.timestamp,
    Math.max(0, Math.round((Number(data.uptimeMs) || 0) / 1000)),
    1,
    data.deviceLabel,
    ''
  ]);

  return {
    session_id: targetId,
    started_at: data.timestamp,
    ended_at: data.timestamp,
    duration_sec: Math.max(0, Math.round((Number(data.uptimeMs) || 0) / 1000)),
    sample_count: 1
  };
}

function fmtManila_(value) {
  if (!value) return '';
  if (Object.prototype.toString.call(value) === '[object Date]') {
    return Utilities.formatDate(value, 'Asia/Manila', 'yyyy-MM-dd HH:mm:ss');
  }
  return String(value);
}

function getSessions() {
  ensureSheets();
  const sheet = getSheet('sessions');
  const values = sheet.getDataRange().getValues();
  const rows = values.slice(1).filter(r => r[0]);

  return {
    ok: true,
    sessions: rows.reverse().map(r => ({
      session_id: r[0],
      session_name: r[1],
      started_at: fmtManila_(r[2]),
      ended_at: fmtManila_(r[3]),
      duration_sec: r[4],
      sample_count: r[5],
      device_label: r[6],
      notes: r[7]
    }))
  };
}

function getReadings(sessionId) {
  ensureSheets();
  const sheet = getSheet('readings');
  const values = sheet.getDataRange().getValues();
  const rows = values.slice(1).filter(r => !sessionId || String(r[0]) === String(sessionId));

  return {
    ok: true,
    session_id: sessionId,
    readings: rows.map(r => ({
      session_id: r[0],
      timestamp: fmtManila_(r[1]),
      uptime_ms: r[2],
      temperature_c: r[3],
      humidity_pct: r[4],
      gas_adc: r[5]
    }))
  };
}

function deleteSession(body) {
  ensureSheets();

  const sessionId = String(body.session_id || '').trim();
  if (!sessionId) {
    return { ok: false, error: 'session_id is required' };
  }

  const sessions = getSheet('sessions');
  const readings = getSheet('readings');

  deleteRowsBySessionId_(readings, sessionId);
  const deletedSessions = deleteRowsBySessionId_(sessions, sessionId);

  return {
    ok: true,
    session_id: sessionId,
    deleted_sessions: deletedSessions,
    deleted_readings: true
  };
}

function deleteRowsBySessionId_(sheet, sessionId) {
  const target = String(sessionId).trim();
  const values = sheet.getDataRange().getValues();
  let deleted = 0;
  for (let i = values.length - 1; i >= 1; i--) {
    if (String(values[i][0]).trim() === target) {
      sheet.deleteRow(i + 1);
      deleted++;
    }
  }
  return deleted;
}
