
## 🧠 Project Overview
A web-based concurrent access system that dynamically categorizes multiple clients as **Readers** or **Writers**, ensuring safe access to a shared SQLite database through synchronization techniques inspired by the **Readers–Writers Problem** in Operating Systems.

- ⚙️ Backend: C (CGI) with SQLite  
- 🌐 Frontend: HTML, CSS, JavaScript  
- 🔒 Synchronization: Database-based semaphore simulation  
- 📊 Real-time statistics and monitoring dashboard

---

## 🧩 Repository Structure

```plaintext
project-root/
├── frontend/      # Web interface (HTML, JS, CSS)
│   └── README.md  # Frontend documentation
├── backend/       # C (CGI) backend with SQLite integration
│   └── README.md  # Backend documentation
└── README.md      # This file (project overview)
```

---

## 🚀 Quick Start

1. **Set up Apache and enable CGI:**
   ```bash
   sudo apt install apache2
   sudo a2enmod cgi
   sudo systemctl restart apache2
   ```

2. **Place files:**
   - Frontend → `/var/www/html/`
   - Backend (`server.cgi` + `chat.db`) → `/usr/lib/cgi-bin/`

3. **Run the app:**
   ```
   http://localhost/CN_Sessional/html/index.html
   ```

---

## 📦 Submodules

| Component | Description |
|------------|-------------|
| [Frontend](./frontend) | User interface, dashboards, and live statistics |
| [Backend](./backend) | C (CGI) system with SQLite-based synchronization |

---

## 👨‍💻 Author
**Indranil Das**  
💬 _B.Tech CSE Project – Reader–Writer Synchronization System_  
