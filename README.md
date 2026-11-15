# PAWtrack — Smart Dog Collar & Canine Telemetry Platform

PAWtrack is an IoT-enabled canine health and welfare monitoring system. It unites precision biometric sensing, satellite geolocation, camera snapshot capability, and a full-stack web dashboard designed for pet owners, animal welfare organizations, and municipal animal birth control (ABC) programs.

---

## 🌟 Key Features

- **Clinical Biometric Telemetry:** Continuous body temperature monitoring via **MAX30205** clinical temperature sensor.
- **Satellite Geolocation:** Real-time positioning via **Ai-Thinker GP-02** GNSS module with map links.
- **Battery Monitoring:** LiPo battery voltage gauge with low-power warnings.
- **Optical Snapshot:** Remote camera frame capture via **ESP32-CAM OV2640**.
- **Full-Stack Architecture:**
  - **Backend:** Node.js + Express REST API with CSV export and SPA routing.
  - **Database:** High-performance persistent SQLite (`node:sqlite`) with WAL mode and foreign key cascades.
  - **Frontend:** Single-page application built on a luxury 60:30:10 light design system with Responsive Web Design (RWD) for mobile, tablet, and desktop.
  - **Zero Login Friction:** Opens directly into the dog directory and management console.
- **Hardware Integration:** Dual Wi-Fi mode supporting router/hotspot connection as well as a standalone hotspot (`Pawtrack-Collar-01`) for field operation.

---

## 📁 Repository Structure

```text
├── db.js                     # SQLite database layer (node:sqlite)
├── server.js                 # Express REST API server
├── package.json              # Node.js project manifest
├── HARDWARE_SETUP.md         # ESP32 & sensor wiring and flashing guide
├── PRD.md                    # Comprehensive Product Requirements Document
├── firmware/
│   └── pawtrack_esp32_collar/
│       └── pawtrack_esp32_collar.ino  # ESP32-CAM Arduino firmware
└── public/
    └── index.html            # Responsive single-page application
```

---

## 🚀 Quick Start

### 1. Run the Web Application

Ensure you have **Node.js (v22.5+ or v24+)** installed:

```bash
# Install dependencies
npm install

# Start the server
npm start
```

Open your browser and navigate to:
```
http://localhost:3000
```

---

### 2. Flash the Collar Hardware

See the full instructions in [HARDWARE_SETUP.md](HARDWARE_SETUP.md).

1. Open `firmware/pawtrack_esp32_collar/pawtrack_esp32_collar.ino` in **Arduino IDE**.
2. Select **AI Thinker ESP32-CAM** (or **ESP32 Dev Module**).
3. Connect your FTDI programmer, tie **GPIO 0 to GND**, and click **Upload**.
4. Disconnect GPIO 0 from GND and open Serial Monitor at **115200 baud** to view the collar's IP.
5. In the web dashboard, enter the collar IP in the Hardware bar and click **Connect**.

---

## 📄 Documentation

- [Product Requirements Document (PRD)](PRD.md)
- [Hardware Wiring & Flashing Guide](HARDWARE_SETUP.md)

---

## 📜 License

MIT License.
