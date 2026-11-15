const { DatabaseSync } = require('node:sqlite');
const path = require('path');

const db = new DatabaseSync(path.join(__dirname, 'pawtrack.db'));

// Enable WAL mode for better concurrency and foreign keys
db.exec('PRAGMA journal_mode = WAL;');
db.exec('PRAGMA foreign_keys = ON;');

// ── Schema ──────────────────────────────────────────────────────────
db.exec(`
  CREATE TABLE IF NOT EXISTS dogs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    breed TEXT DEFAULT '',
    age REAL DEFAULT NULL,
    weight REAL DEFAULT NULL,
    marks TEXT DEFAULT '',
    sterilized TEXT DEFAULT 'unknown' CHECK(sterilized IN ('yes','no','unknown')),
    vaccinated TEXT DEFAULT 'unknown' CHECK(vaccinated IN ('yes','no','unknown')),
    temperament TEXT DEFAULT 'Friendly',
    status TEXT DEFAULT 'On the street' CHECK(status IN ('On the street','Adopted','In shelter','Missing')),
    area TEXT DEFAULT '',
    collar_id TEXT DEFAULT '',
    photo TEXT DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  );

  CREATE TABLE IF NOT EXISTS readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    dog_id INTEGER NOT NULL,
    ts DATETIME DEFAULT CURRENT_TIMESTAMP,
    temp REAL,
    batt INTEGER,
    lat REAL,
    lng REAL,
    alert TEXT DEFAULT '',
    FOREIGN KEY (dog_id) REFERENCES dogs(id) ON DELETE CASCADE
  );

  CREATE INDEX IF NOT EXISTS idx_readings_dog_ts ON readings(dog_id, ts DESC);
`);

// ── Prepared statements ─────────────────────────────────────────────
const stmts = {
  allDogs: db.prepare('SELECT * FROM dogs ORDER BY created_at DESC'),
  dogById: db.prepare('SELECT * FROM dogs WHERE id = ?'),
  insertDog: db.prepare(`
    INSERT INTO dogs (name, breed, age, weight, marks, sterilized, vaccinated, temperament, status, area, collar_id, photo)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  `),
  updateDog: db.prepare(`
    UPDATE dogs SET
      name = ?,
      breed = ?,
      age = ?,
      weight = ?,
      marks = ?,
      sterilized = ?,
      vaccinated = ?,
      temperament = ?,
      status = ?,
      area = ?,
      collar_id = ?,
      photo = ?
    WHERE id = ?
  `),
  deleteDog: db.prepare('DELETE FROM dogs WHERE id = ?'),
  readingsAll: db.prepare('SELECT * FROM readings WHERE dog_id = ? ORDER BY ts DESC LIMIT 500'),
  readingsRange: db.prepare(`
    SELECT * FROM readings
    WHERE dog_id = ? AND ts >= datetime('now', ?)
    ORDER BY ts DESC LIMIT 500
  `),
  insertReading: db.prepare(`
    INSERT INTO readings (dog_id, temp, batt, lat, lng, alert)
    VALUES (?, ?, ?, ?, ?, ?)
  `),
  latestReading: db.prepare('SELECT * FROM readings WHERE dog_id = ? ORDER BY ts DESC LIMIT 1'),
};

// ── Exported helpers ────────────────────────────────────────────────
function getAllDogs() {
  return stmts.allDogs.all();
}

function getDogById(id) {
  const result = stmts.dogById.get(id);
  return result || null;
}

function createDog(data) {
  const params = [
    data.name,
    data.breed ?? '',
    data.age ?? null,
    data.weight ?? null,
    data.marks ?? '',
    data.sterilized ?? 'unknown',
    data.vaccinated ?? 'unknown',
    data.temperament ?? 'Friendly',
    data.status ?? 'On the street',
    data.area ?? '',
    data.collar_id ?? '',
    data.photo ?? '',
  ];
  const info = stmts.insertDog.run(...params);
  return getDogById(Number(info.lastInsertRowid));
}

function updateDog(id, data) {
  const existing = getDogById(id);
  if (!existing) return null;
  const params = [
    data.name ?? existing.name,
    data.breed ?? existing.breed,
    data.age !== undefined ? data.age : existing.age,
    data.weight !== undefined ? data.weight : existing.weight,
    data.marks ?? existing.marks,
    data.sterilized ?? existing.sterilized,
    data.vaccinated ?? existing.vaccinated,
    data.temperament ?? existing.temperament,
    data.status ?? existing.status,
    data.area ?? existing.area,
    data.collar_id ?? existing.collar_id,
    data.photo ?? existing.photo,
    id,
  ];
  stmts.updateDog.run(...params);
  return getDogById(id);
}

function deleteDog(id) {
  return stmts.deleteDog.run(id);
}

function getReadings(dogId, hoursAgo = 0) {
  if (hoursAgo > 0) {
    return stmts.readingsRange.all(dogId, `-${hoursAgo} hours`);
  }
  return stmts.readingsAll.all(dogId);
}

function addReading(dogId, data) {
  const params = [
    dogId,
    data.temp ?? null,
    data.batt ?? null,
    data.lat ?? null,
    data.lng ?? null,
    data.alert ?? '',
  ];
  const info = stmts.insertReading.run(...params);
  return {
    id: Number(info.lastInsertRowid),
    dog_id: dogId,
    temp: data.temp ?? null,
    batt: data.batt ?? null,
    lat: data.lat ?? null,
    lng: data.lng ?? null,
    alert: data.alert ?? '',
  };
}

function getLatestReading(dogId) {
  const result = stmts.latestReading.get(dogId);
  return result || null;
}

module.exports = {
  db,
  getAllDogs,
  getDogById,
  createDog,
  updateDog,
  deleteDog,
  getReadings,
  addReading,
  getLatestReading,
};
