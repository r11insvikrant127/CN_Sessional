Frontend System - Concurrent Chat Application
Modern, responsive frontend interface for the concurrent chat system with real-time monitoring and analytics.

📁 File Structure
frontend/
├── index.html              # Main dashboard with system overview
├── reader.html             # Reader interface for viewing messages
├── writer.html             # Writer interface for posting messages
├── dashboard.html          # Real-time monitoring dashboard
├── stats.html              # Advanced statistics and analytics
├── style.css               # Comprehensive CSS styling with themes
├── script.js               # Core JavaScript utilities and theme management
├── dashboard.js            # Real-time dashboard functionality
└── stats.js               # Advanced statistics and chart management

🎨 Design System
Theme Support
  a. Light/Dark Mode with seamless switching
  b. CSS Custom Properties for consistent theming
  c. Smooth transitions and animations
  d. Responsive design for all screen sizes


📄 Page Overview

1. index.html - Main Dashboard
   
i. Purpose: System overview and navigation hub
ii. Features:

  a. Real-time stats display (active readers/writers, total operations)
  b. 2x2 action grid for navigation
  c. System architecture visualization
  d. Auto-updating statistics every second
  e. Theme toggle functionality

iii. Key Components:

  a. Hero section with gradient title
  b. Stats overview cards
  c. Action cards (Reader, Writer, Dashboard, Statistics)
  d. Architecture diagram


2. reader.html - Reader Interface

i. Purpose: View chat messages with concurrent read access
ii. Features:
  a. Real-time message display
  b. Active reader count tracking
  c. Auto-refresh capability
  d. Reader access rules information
  e. Error handling with retry functionality

iii. Key Components:
  a. Messages container with scrollable list
  b. Message cards with user avatars and timestamps
  c. Reader statistics badge
  d. Loading and error states


3. writer.html - Writer Interface

i. Purpose: Post new messages with exclusive write access
ii. Features:
  a. Message form with character counting
  b. Real-time writer count display
  c. Form validation and submission
  d. Writer access rules information
  e. Success/error handling

iii. Key Components:
  a. Message form with username and message fields
  b. Character counter (0/256)
  c. Writer statistics badge
  d. Form submission with loading states


4. dashboard.html - Real-time Monitoring

i. Purpose: Live system monitoring and performance tracking
ii. Features:
  a. Real-time active clients chart
  b. Operations distribution doughnut chart
  c. Performance metrics bar chart
  d. System status indicators
  e. Auto-refresh every 2-5 seconds

iii. Key Components:
  a. Live indicator with pulse animation
  b. Large stat cards with trends
  c. Interactive Chart.js visualizations
  d. Status grid for system health


5. stats.html - Advanced Analytics

i. Purpose: Comprehensive system analytics and historical data
ii. Features:
  a. Operations timeline chart (24 hours)
  b. Performance heatmap (daily load)
  c. Efficiency radar chart (reader vs writer)
  d. Performance insights cards
  e. Smart update intervals with error handling

iii. Key Components:
  a. Metrics grid with highlight cards
  b. Multiple chart types (line, bar, radar)
  c. Performance insights grid
  d. Responsive chart layouts


🛠️ JavaScript Modules

1. script.js - Core Utilities

i. Theme Management:
  a. ThemeManager class for theme switching
  b. Local storage persistence
  c. Icon updates based on current theme

2. Utility Functions:
  a. Number formatting with commas
  b. Debounce for performance optimization
  c. Loading state management


2. dashboard.js - Real-time Dashboard

i. Dashboard Class:
  a. Real-time chart management
  b. Live data updates every second
  c. Error handling with fallback data
  d. Smooth chart animations

ii. Key Features:
  a. 10-point data limit for performance
  b. Real-time active clients tracking
  c. Automatic chart updates
  d. Responsive design support


3. stats.js - Advanced Statistics

i. Statistics Class:
  a. Multiple chart management
  b. Smart update intervals with debouncing
  c. Error recovery mechanisms
  d. Performance optimization

ii. Key Features:
  a. Preloading initial data
  b. Adjustable update intervals based on error count
  c. Smooth chart transitions
  d. Memory management and cleanup


📊 Chart Implementations

i. Chart.js Integration
  a. Line Charts: Real-time active clients, operations timeline
  b. Doughnut Charts: Operations distribution
  c. Bar Charts: Performance metrics, daily load heatmap
  d. Radar Charts: Efficiency comparisons

ii. Real-time Features
  a. Auto-refresh intervals (1s to 30s)
  b. Data point limiting for performance
  c. Smooth animations and transitions
  d. Error handling with fallback displays


🔄 API Integration

Backend Endpoints Used:

1. /CN_Sessional/cgi-bin/server.cgi/status
2. /CN_Sessional/cgi-bin/server.cgi/reader  
3. /CN_Sessional/cgi-bin/server.cgi/writer
4. /CN_Sessional/cgi-bin/server.cgi/historical
5. /CN_Sessional/cgi-bin/server.cgi/live-active-clients
6. /CN_Sessional/cgi-bin/server.cgi/performance
7. /CN_Sessional/cgi-bin/server.cgi/daily-load


Data Flow:

1. Reader: Fetches server-rendered HTML for messages
2. Writer: Submits form data via POST, receives HTML response
3. Dashboard: JSON API calls for real-time data
4. Stats: Multiple JSON endpoints for analytics data
