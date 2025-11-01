# 💻 Frontend System — Concurrent Chat Application

A **modern, responsive frontend interface** for the Concurrent Chat System featuring real-time monitoring, analytics visualization, and dynamic user interaction.

---

## 📁 File Structure

frontend/
├── index.html # Main dashboard with system overview
├── reader.html # Reader interface for viewing messages
├── writer.html # Writer interface for posting messages
├── dashboard.html # Real-time monitoring dashboard
├── stats.html # Advanced statistics and analytics
├── style.css # Comprehensive CSS styling with themes
├── script.js # Core JavaScript utilities and theme management
├── dashboard.js # Real-time dashboard functionality
└── stats.js # Advanced statistics and chart management

---

## 🎨 Design System

### 🌗 Theme Support
- Light/Dark Mode with seamless switching  
- CSS Custom Properties for consistent theming  
- Smooth transitions and animations  
- Fully responsive design for all screen sizes  

---

## 📄 Page Overview

### 1. 🏠 `index.html` — Main Dashboard

**Purpose:** System overview and navigation hub  

**Features:**
- Real-time statistics display (active readers/writers, total operations)  
- 2×2 navigation grid  
- System architecture visualization  
- Auto-updating statistics every second  
- Theme toggle functionality  

**Key Components:**
- Hero section with gradient title  
- Statistics overview cards  
- Action cards (Reader, Writer, Dashboard, Statistics)  
- Architecture diagram  

---

### 2. 📖 `reader.html` — Reader Interface

**Purpose:** View chat messages with concurrent read access  

**Features:**
- Real-time message updates  
- Active reader count tracking  
- Auto-refresh capability  
- Reader access rules and info section  
- Error handling with retry support  

**Key Components:**
- Scrollable message container  
- Message cards with avatars and timestamps  
- Reader statistics badge  
- Loading and error states  

---

### 3. ✍️ `writer.html` — Writer Interface

**Purpose:** Post new messages with exclusive write access  

**Features:**
- Message form with character counter  
- Real-time writer count display  
- Form validation and submission  
- Writer access rules and guidance  
- Success/error handling  

**Key Components:**
- Message form (username + message fields)  
- Character counter (0/256)  
- Writer statistics badge  
- Form submission with loading indicators  

---

### 4. 📊 `dashboard.html` — Real-Time Monitoring

**Purpose:** Live system monitoring and performance tracking  

**Features:**
- Real-time active clients line chart  
- Operations distribution doughnut chart  
- Performance metrics bar chart  
- System status indicators  
- Auto-refresh every 2–5 seconds  

**Key Components:**
- Live indicator with pulse animation  
- Trend cards with large numeric stats  
- Interactive Chart.js visualizations  
- System health grid  

---

### 5. 📈 `stats.html` — Advanced Analytics

**Purpose:** Comprehensive analytics and historical performance overview  

**Features:**
- 24-hour operations timeline chart  
- Daily load heatmap visualization  
- Efficiency radar chart (reader vs writer)  
- Performance insights cards  
- Smart update intervals with retry logic  

**Key Components:**
- Metrics grid with highlight cards  
- Multiple chart types (line, bar, radar)  
- Performance insights panel  
- Responsive chart layout  

---

## 🛠️ JavaScript Modules

### 1. `script.js` — Core Utilities

**Theme Management**
- `ThemeManager` class for theme switching  
- Local-storage persistence for user preference  
- Dynamic icon updates based on theme  

**Utility Functions**
- Number formatting with commas  
- Debounce for optimized rendering  
- Loading and error state management  

---

### 2. `dashboard.js` — Real-Time Dashboard Logic

**Dashboard Class**
- Manages all live charts dynamically  
- Updates data every second  
- Handles errors gracefully with fallback data  
- Smooth transition animations  

**Key Features**
- 10-point rolling data window for performance  
- Real-time active clients tracking  
- Automatic chart updates and refresh intervals  
- Full responsive support  

---

### 3. `stats.js` — Advanced Statistics Module

**Statistics Class**
- Manages multiple Chart.js instances  
- Smart update intervals with debounce  
- Error recovery mechanisms  
- Optimized for memory and performance  

**Key Features**
- Preloading of initial analytics data  
- Adjustable update intervals based on errors  
- Smooth chart transitions and cleanup  
- Efficient memory management  

---

## 📊 Chart Implementations

### Chart.js Integration
| Chart Type | Usage |
|-------------|--------|
| **Line Charts** | Real-time active clients, operations timeline |
| **Doughnut Charts** | Operations distribution visualization |
| **Bar Charts** | Performance metrics, daily load heatmap |
| **Radar Charts** | Reader vs Writer efficiency comparison |

### Real-Time Features
- Auto-refresh intervals (1 s – 30 s)  
- Data point limits for performance  
- Smooth animations and transitions  
- Fallback visuals for temporary data loss  

---

## 🔄 API Integration

### Backend Endpoints Used
| Endpoint | Purpose |
|-----------|----------|
| `/CN_Sessional/cgi-bin/server.cgi/status` | Fetches current reader/writer counts |
| `/CN_Sessional/cgi-bin/server.cgi/reader` | Fetches chat messages (Reader view) |
| `/CN_Sessional/cgi-bin/server.cgi/writer` | Submits new messages (Writer view) |
| `/CN_Sessional/cgi-bin/server.cgi/historical` | Retrieves historical operations data |
| `/CN_Sessional/cgi-bin/server.cgi/live-active-clients` | Returns live activity data |
| `/CN_Sessional/cgi-bin/server.cgi/performance` | Provides performance metrics |
| `/CN_Sessional/cgi-bin/server.cgi/daily-load` | Provides daily load and concurrency stats |

---

### Data Flow Overview

| Module | Data Source | Description |
|---------|--------------|-------------|
| **Reader** | HTML (server-rendered) | Fetches messages and updates live feed |
| **Writer** | POST form | Submits message content and updates display |
| **Dashboard** | JSON API | Fetches active client and operation metrics |
| **Statistics** | JSON API | Fetches analytics, heatmaps, and performance metrics |

---

## 🧩 Summary

This frontend delivers:
- 🎨 Elegant light/dark UI design  
- ⚡ Real-time dashboards and visualizations  
- 🧠 Smart JavaScript modules for analytics  
- 🔗 Seamless integration with CGI backend APIs  
- 📱 Fully responsive layout across all devices  

---

**Author:** Indranil Das  
**Frameworks:** HTML 5, CSS 3, JavaScript (ES6 + Chart.js)  
**License:** MIT  
**Platform:** Browser (desktop + mobile responsive)

