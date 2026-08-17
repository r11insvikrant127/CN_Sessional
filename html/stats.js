class Statistics {
    constructor() {
        this.charts = {};
        this.updateIntervals = {};
        this.errorCount = 0;
        this.isUpdating = {};
        this.updateQueue = {};
        this.init();
    }

    init() {
        this.createCharts();
        this.preloadInitialData();
        this.setupSmartUpdates();
    }

    createCharts() {
        // Timeline Chart - Initialize with loading state
        const timelineCtx = document.getElementById('timelineChart').getContext('2d');
        this.charts.timeline = new Chart(timelineCtx, {
            type: 'line',
            data: {
                labels: ['Loading...'],
                datasets: [
                    {
                        label: 'Read Operations',
                        data: [0],
                        borderColor: '#3b82f6',
                        backgroundColor: 'rgba(59, 130, 246, 0.1)',
                        tension: 0.4,
                        fill: true
                    },
                    {
                        label: 'Write Operations',
                        data: [0],
                        borderColor: '#8b5cf6',
                        backgroundColor: 'rgba(139, 92, 246, 0.1)',
                        tension: 0.4,
                        fill: true
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    intersect: false,
                    mode: 'index'
                },
                plugins: {
                    legend: {
                        position: 'top',
                    },
           	 tooltip: {
           	     mode: 'index',
           	     intersect: false,
           	     callbacks: {
           	         title: (tooltipItems) => {
           	             // FIX: Convert end time to start time range for display
           	             const endTime = tooltipItems[0].label; // e.g., "21:00"
           	             const hour = parseInt(endTime.split(':')[0]);
           	             const startHour = (hour - 1 + 24) % 24; // Subtract 1 hour
           	             return `${startHour.toString().padStart(2, '0')}:00 - ${endTime}`;
           	             // Now shows "20:00 - 21:00" in tooltip instead of just "21:00"
           	         }
           	     }
           	 }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Number of Operations'
                        }
                    }
                }
            }
        });

        // Heatmap Chart - Initialize with loading state
        const heatmapCtx = document.getElementById('heatmapChart').getContext('2d');
        this.charts.heatmap = new Chart(heatmapCtx, {
            type: 'bar',
            data: {
                labels: ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'],
                datasets: [{
                    label: 'System Load',
                    data: [0, 0, 0, 0, 0, 0, 0],
                    backgroundColor: [
                        'rgba(59, 130, 246, 0.8)',
                        'rgba(59, 130, 246, 0.9)',
                        'rgba(59, 130, 246, 0.8)',
                        'rgba(59, 130, 246, 1.0)',
                        'rgba(59, 130, 246, 1.0)',
                        'rgba(59, 130, 246, 0.6)',
                        'rgba(59, 130, 246, 0.6)'
                    ]
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: false
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        max: 100,
                        title: {
                            display: true,
                            text: 'Load Percentage'
                        }
                    }
                }
            }
        });

                // === REAL PERFORMANCE METRICS ===
        /*
         * Performance metrics use their actual units.
         *
         * We intentionally do NOT combine latency (ms), throughput
         * (ops/sec), percentages, and concurrency into a single
         * 0-100 score because doing so would create an artificial
         * combined metric.
         */
        this.charts.efficiency = null;
    }

    async preloadInitialData() {
        try {
            await this.loadBasicStats();
            await this.delay(100);
            await this.loadHistoricalData();
            await this.delay(100);
            await this.loadDailyLoad();
            await this.delay(100);
            await this.loadPerformanceMetrics();
            
            this.errorCount = 0;
        } catch (error) {
            this.handleDataLoadError('initial load');
        }
    }

    setupSmartUpdates() {
        this.updateIntervals = {
            basicStats: this.setupDebouncedUpdate(() => this.loadBasicStats(), 3000),
            historical: this.setupDebouncedUpdate(() => this.loadHistoricalData(), 10000),
            dailyLoad: this.setupDebouncedUpdate(() => this.loadDailyLoad(), 30000),
            performance: this.setupDebouncedUpdate(() => this.loadPerformanceMetrics(), 20000)
        };
    }

    setupDebouncedUpdate(updateFunction, interval) {
        let timeoutId;
        const executeUpdate = () => {
            if (!this.isUpdating[updateFunction.name]) {
                this.isUpdating[updateFunction.name] = true;
                updateFunction().finally(() => {
                    this.isUpdating[updateFunction.name] = false;
                });
            }
        };

        return setInterval(() => {
            clearTimeout(timeoutId);
            timeoutId = setTimeout(executeUpdate, 100);
        }, interval);
    }

    updateChartSmoothly(chartName, updateCallback) {
        if (this.updateQueue[chartName]) {
            return;
        }

        this.updateQueue[chartName] = true;
        
        requestAnimationFrame(() => {
            try {
                updateCallback();
                this.charts[chartName].update('none');
            } catch (error) {
                console.error(`Error updating ${chartName}:`, error);
            } finally {
                this.updateQueue[chartName] = false;
            }
        });
    }

    handleDataLoadError(chartType) {
        this.errorCount++;
        console.error(`Error loading ${chartType} data`);
        
        this.showErrorState(chartType);
        
        if (this.errorCount > 3) {
            this.adjustUpdateIntervals(30000);
        }
        
        if (this.errorCount === 5) {
            setTimeout(() => {
                this.errorCount = 0;
                this.adjustUpdateIntervals(5000);
            }, 60000);
        }
    }

    showErrorState(chartType) {
        console.warn(`Chart data load failed for: ${chartType}`);
    }

    adjustUpdateIntervals(newInterval) {
        Object.values(this.updateIntervals).forEach(clearInterval);
        
        this.updateIntervals = {
            basicStats: this.setupDebouncedUpdate(() => this.loadBasicStats(), newInterval),
            historical: this.setupDebouncedUpdate(() => this.loadHistoricalData(), newInterval * 2),
            dailyLoad: this.setupDebouncedUpdate(() => this.loadDailyLoad(), newInterval * 3),
            performance: this.setupDebouncedUpdate(() => this.loadPerformanceMetrics(), newInterval * 2)
        };
    }

    async loadBasicStats() {
        try {
            const response = await fetch('/CN_Sessional/cgi-bin/server.cgi/status');
            if (!response.ok) throw new Error('Network response was not ok');
            
            const data = await response.json();
            if (!data.error) {
                this.updateMetrics(data);
                this.errorCount = Math.max(0, this.errorCount - 1);
            } else {
                throw new Error(data.error);
            }
        } catch (error) {
            this.handleDataLoadError('basic statistics');
        }
    }

    async loadHistoricalData() {
        try {
            const response = await fetch('/CN_Sessional/cgi-bin/server.cgi/historical');
            if (!response.ok) throw new Error('Network response was not ok');
            
            const data = await response.json();
            if (data.timestamps && data.reads && data.writes) {
                this.updateChartSmoothly('timeline', () => {
                    this.charts.timeline.data.labels = data.timestamps;
                    this.charts.timeline.data.datasets[0].data = data.reads;
                    this.charts.timeline.data.datasets[1].data = data.writes;
                });
            }
        } catch (error) {
            this.handleDataLoadError('historical data');
        }
    }

    async loadDailyLoad() {
        try {
            const response = await fetch('/CN_Sessional/cgi-bin/server.cgi/daily-load');
            if (!response.ok) throw new Error('Network response was not ok');
            
            const data = await response.json();
            if (data.daily_load) {
                this.updateChartSmoothly('heatmap', () => {
                    this.charts.heatmap.data.datasets[0].data = data.daily_load;
                });
            }
        } catch (error) {
            this.handleDataLoadError('daily load data');
        }
    }

    async loadPerformanceMetrics() {
        try {
            const response =
                await fetch('/CN_Sessional/cgi-bin/server.cgi/performance');

            if (!response.ok) {
                throw new Error('Network response was not ok');
            }

            const data = await response.json();

            const container =
                document.getElementById('efficiencyMetrics');

            if (!container) {
                throw new Error('Performance metrics container not found');
            }

            const formatMs = value =>
                Number(value || 0).toFixed(2) + ' ms';

            const formatPercent = value =>
                Number(value || 0).toFixed(2) + '%';

            const formatOps = value =>
                Number(value || 0).toFixed(2) + ' ops/sec';

            const formatClients = value =>
                Number(value || 0).toFixed(0) + ' clients';

            container.innerHTML = `
                <div class="performance-metric-group">
                    <h4>Reader</h4>

                    <div class="performance-metric-row">
                        <span>Average latency</span>
                        <strong>${formatMs(data.reader_speed_ms)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Reliability</span>
                        <strong>${formatPercent(data.reader_reliability_percent)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Maximum concurrency</span>
                        <strong>${formatClients(data.reader_max_concurrency)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Throughput</span>
                        <strong>${formatOps(data.reader_throughput_ops_sec)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Latency consistency</span>
                        <strong>${formatMs(data.reader_consistency_ms)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Availability</span>
                        <strong>${formatPercent(data.reader_availability_percent)}</strong>
                    </div>
                </div>

                <div class="performance-metric-group">
                    <h4>Writer</h4>

                    <div class="performance-metric-row">
                        <span>Average latency</span>
                        <strong>${formatMs(data.writer_speed_ms)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Reliability</span>
                        <strong>${formatPercent(data.writer_reliability_percent)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Maximum concurrency</span>
                        <strong>${formatClients(data.writer_max_concurrency)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Throughput</span>
                        <strong>${formatOps(data.writer_throughput_ops_sec)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Latency consistency</span>
                        <strong>${formatMs(data.writer_consistency_ms)}</strong>
                    </div>

                    <div class="performance-metric-row">
                        <span>Availability</span>
                        <strong>${formatPercent(data.writer_availability_percent)}</strong>
                    </div>
                </div>
            `;

        } catch (error) {
            this.handleDataLoadError('performance metrics');
        }
    }

    updateMetrics(data) {
        document.getElementById('totalOps').textContent = data.totalReads + data.totalWrites;
        document.getElementById('statsReads').textContent = data.totalReads;
        document.getElementById('statsWrites').textContent = data.totalWrites;
        
        const ratio = data.totalWrites > 0 ? (data.totalReads / data.totalWrites).toFixed(1) : data.totalReads;
        document.getElementById('readWriteRatio').textContent = `${ratio}:1`;
    }

    delay(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    destroy() {
        Object.values(this.updateIntervals).forEach(clearInterval);
    }
}

// Initialize statistics when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    window.statsManager = new Statistics();
});

// Clean up on page unload
window.addEventListener('beforeunload', () => {
    if (window.statsManager) {
        window.statsManager.destroy();
    }
});
