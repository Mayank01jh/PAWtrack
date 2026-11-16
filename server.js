const express = require('express');
const cors = require('cors');
const multer = require('multer');
const path = require('path');
const {
  getAllDogs,
  getDogById,
  createDog,
  updateDog,
  deleteDog,
  getReadings,
  addReading,
  getLatestReading,
} = require('./db');

const app = express();
const PORT = process.env.PORT || 3000;
const DEFAULT_FIREBASE_URL = process.env.FIREBASE_URL || 'https://dog-monitoring-74337-default-rtdb.firebaseio.com/collar_data.json';

// ── Middleware ───────────────────────────────────────────────────────
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Multer — memory storage, 2 MB limit
const upload = multer({
  storage: multer.memoryStorage(),
  limits: { fileSize: 2 * 1024 * 1024 },
});

// ── API Routes ──────────────────────────────────────────────────────

// List all dogs
app.get('/api/dogs', (_req, res) => {
  res.json(getAllDogs());
});

// Get single dog + latest reading
app.get('/api/dogs/:id', (req, res) => {
  const dog = getDogById(Number(req.params.id));
  if (!dog) return res.status(404).json({ error: 'Dog not found' });
  const latestReading = getLatestReading(dog.id);
  res.json({ ...dog, latestReading });
});

// Create dog (multipart form with optional photo)
app.post('/api/dogs', upload.single('photo'), (req, res) => {
  try {
    const data = { ...req.body };
    if (req.file) {
      const b64 = req.file.buffer.toString('base64');
      data.photo = `data:${req.file.mimetype};base64,${b64}`;
    }
    if (!data.name) return res.status(400).json({ error: 'name is required' });
    // Coerce numeric fields
    if (data.age !== undefined && data.age !== '') data.age = Number(data.age);
    else data.age = null;
    if (data.weight !== undefined && data.weight !== '') data.weight = Number(data.weight);
    else data.weight = null;
    const dog = createDog(data);
    res.status(201).json(dog);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Update dog
app.put('/api/dogs/:id', upload.single('photo'), (req, res) => {
  try {
    const data = { ...req.body };
    if (req.file) {
      const b64 = req.file.buffer.toString('base64');
      data.photo = `data:${req.file.mimetype};base64,${b64}`;
    }
    if (data.age !== undefined && data.age !== '') data.age = Number(data.age);
    if (data.weight !== undefined && data.weight !== '') data.weight = Number(data.weight);
    const dog = updateDog(Number(req.params.id), data);
    if (!dog) return res.status(404).json({ error: 'Dog not found' });
    res.json(dog);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Delete dog (cascade deletes readings via FK)
app.delete('/api/dogs/:id', (req, res) => {
  const dog = getDogById(Number(req.params.id));
  if (!dog) return res.status(404).json({ error: 'Dog not found' });
  deleteDog(Number(req.params.id));
  res.json({ success: true });
});

// Get readings for a dog
app.get('/api/dogs/:id/readings', (req, res) => {
  const dogId = Number(req.params.id);
  if (!getDogById(dogId)) return res.status(404).json({ error: 'Dog not found' });
  const range = Number(req.query.range) || 0;
  res.json(getReadings(dogId, range));
});

// Log a new reading
app.post('/api/dogs/:id/readings', (req, res) => {
  const dogId = Number(req.params.id);
  if (!getDogById(dogId)) return res.status(404).json({ error: 'Dog not found' });
  try {
    const reading = addReading(dogId, req.body);
    res.status(201).json(reading);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// SOS — creates a reading with alert='SOS triggered'
app.post('/api/dogs/:id/sos', (req, res) => {
  const dogId = Number(req.params.id);
  if (!getDogById(dogId)) return res.status(404).json({ error: 'Dog not found' });
  try {
    const reading = addReading(dogId, {
      ...req.body,
      alert: 'SOS triggered',
    });
    res.status(201).json(reading);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── Firebase Cloud Integration ───────────────────────────────────────

// Proxy current collar data from Firebase Realtime Database
app.get('/api/firebase/data', async (req, res) => {
  let url = req.query.url || DEFAULT_FIREBASE_URL;
  if (!url.endsWith('.json')) url = url.replace(/\/+$/, '') + '/collar_data.json';
  try {
    const fRes = await fetch(url, { signal: AbortSignal.timeout(5000) });
    if (!fRes.ok) throw new Error(`Firebase returned HTTP ${fRes.status}`);
    const data = await fRes.json();
    res.json(data || {});
  } catch (err) {
    res.status(502).json({ error: `Firebase fetch failed: ${err.message}` });
  }
});

// Sync current Firebase data into a specific dog's telemetry record
app.post('/api/dogs/:id/firebase-sync', async (req, res) => {
  const dogId = Number(req.params.id);
  if (!getDogById(dogId)) return res.status(404).json({ error: 'Dog not found' });
  let url = req.body?.url || DEFAULT_FIREBASE_URL;
  if (!url.endsWith('.json')) url = url.replace(/\/+$/, '') + '/collar_data.json';
  try {
    const fRes = await fetch(url, { signal: AbortSignal.timeout(5000) });
    if (!fRes.ok) throw new Error(`Firebase returned HTTP ${fRes.status}`);
    const data = await fRes.json();
    if (!data) return res.json({ message: 'No data currently in Firebase' });

    let alert = '';
    const bodyTemp = Number(data.body_temp);
    if (bodyTemp > 39.5) alert = 'Critical Hyperthermia (> 39.5°C)';
    else if (bodyTemp > 0 && bodyTemp < 37.0) alert = 'Hypothermia Warning (< 37.0°C)';

    const reading = addReading(dogId, {
      temp: data.body_temp,
      ambient_temp: data.ambient_temp,
      humidity: data.humidity,
      lat: data.latitude,
      lng: data.longitude,
      batt: data.batt ?? null,
      alert,
    });
    res.json({ success: true, reading, raw: data });
  } catch (err) {
    res.status(502).json({ error: err.message });
  }
});

// CSV export of readings
app.get('/api/dogs/:id/readings/export', (req, res) => {
  const dogId = Number(req.params.id);
  if (!getDogById(dogId)) return res.status(404).json({ error: 'Dog not found' });
  const range = Number(req.query.range) || 0;
  const rows = getReadings(dogId, range);

  res.setHeader('Content-Type', 'text/csv');
  res.setHeader('Content-Disposition', 'attachment; filename=collar-readings.csv');

  // Header row
  res.write('id,dog_id,ts,body_temp,ambient_temp,humidity,batt,lat,lng,alert\n');
  for (const r of rows) {
    const line = [
      r.id,
      r.dog_id,
      r.ts,
      r.temp ?? '',
      r.ambient_temp ?? '',
      r.humidity ?? '',
      r.batt ?? '',
      r.lat ?? '',
      r.lng ?? '',
      `"${(r.alert || '').replace(/"/g, '""')}"`
    ].join(',');
    res.write(line + '\n');
  }
  res.end();
});

// ── SPA fallback ────────────────────────────────────────────────────
app.get('*', (_req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// ── Start ───────────────────────────────────────────────────────────
app.listen(PORT, () => {
  console.log(`🐾 Pawtrack server running at http://localhost:${PORT}`);
});
