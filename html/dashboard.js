class Dashboard {
    constructor() {
        this.clientsChart = null;
        this.operationsChart = null;
        this.performanceChart = null;
        this.maxDataPoints = 10; // Maximum number of data points to show
        this.init();
    }

    init() {
	    this.createCharts();
	    this.startLiveUpdates();
	    this.updateDashboard();
	    this.updatePerformanceChart();
	    this.startRealTimeUpdates();
	}

    createCharts() {
        // === REAL-TIME ACTIVE CLIENTS CHART ===
        const clientsCtx = document.getElementById('clientsChart').getContext('2d');
        this.clientsChart = new Chart(clientsCtx, {
            type: 'line',
            data: {
                labels: [], // timestamps (HH:MM:SS)
                datasets: [
                    {
                        label: 'Active Readers',
                        data: [],
                        borderColor: '#3b82f6',
                        backgroundColor: 'rgba(59, 130, 246, 0.1)',
                        borderWidth: 2,
                        tension: 0.4,
                        fill: true
                    },
                    {
                        label: 'Active Writers',
                        data: [],
                        borderColor: '#ef4444',
                        backgroundColor: 'rgba(239, 68, 68, 0.1)',
                        borderWidth: 2,
                        tension: 0.4,
                        fill: true
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: { position: 'top' },
                    title: {
                        display: true,
                        text: 'Active Clients Over Time (Real-time)'
                    }
                },
                scales: {
                    x: {
                        display: true,
                        title: {
                            display: true,
                            text: 'Time (Local HH:MM:SS)'
                        },
                        ticks: {
                            color: '#9ca3af', // gray tone, visible in both themes
                            maxRotation: 45,
                            minRotation: 45
                        },
                        grid: {
                            color: 'rgba(128,128,128,0.2)'
                        }
                    },
                    y: {
                        beginAtZero: true,
                        title: { display: true, text: 'Active Clients' },
                        ticks: { stepSize: 1 },
                        suggestedMax: 5,
                        grid: { color: 'rgba(128,128,128,0.2)' }
                    }
                },
                animation: { duration: 0 }
            }
        });

        // === OPERATIONS DISTRIBUTION CHART ===
        const operationsCtx = document.getElementById('operationsChart').getContext('2d');
        this.operationsChart = new Chart(operationsCtx, {
            type: 'doughnut',
            data: {
                labels: ['Read Operations', 'Write Operations'],
                datasets: [{
                    data: [0, 0],
                    backgroundColor: [
                        'rgba(59, 130, 246, 0.8)',
                        'rgba(239, 68, 68, 0.8)'
                    ],
                    borderColor: [
                        'rgb(59, 130, 246)',
                        'rgb(239, 68, 68)'
                    ],
                    borderWidth: 2
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { position: 'bottom' } }
            }
        });

        // === PERFORMANCE METRICS CHART ===
        const performanceCtx =
            document.getElementById('performanceChart').getContext('2d');

        this.performanceChart = new Chart(performanceCtx, {
            type: 'bar',
            data: {
                labels: [
                    'Reader Latency',
                    'Writer Latency',
                    'Reader Throughput',
                    'Writer Throughput',
                    'Reader Reliability',
                    'Writer Reliability'
                ],
                datasets: [{
                    label: 'Measured Value',
                    data: [0, 0, 0, 0, 0, 0],
                    backgroundColor: [
                        'rgba(59, 130, 246, 0.8)',
                        'rgba(239, 68, 68, 0.8)',
                        'rgba(16, 185, 129, 0.8)',
                        'rgba(245, 158, 11, 0.8)',
                        'rgba(139, 92, 246, 0.8)',
                        'rgba(234, 88, 12, 0.8)'
                    ],
                    borderWidth: 2
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                indexAxis: 'y',
                plugins: {
                    legend: {
                        display: false
                    },
                    tooltip: {
                        callbacks: {
                            label: function(context) {
                                const index = context.dataIndex;
                                const value = context.raw;

                                const units = [
                                    ' ms',
                                    ' ms',
                                    ' ops/sec',
                                    ' ops/sec',
                                    ' %',
                                    ' %'
                                ];

                                return `${value}${units[index]}`;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Measured Value'
                        },
                        grid: {
                            color: 'rgba(128,128,128,0.2)'
                        }
                    },
                    y: {
                        grid: {
                            color: 'rgba(128,128,128,0.2)'
                        }
                    }
                }
            }
        });
    }

    // === UPDATE CHART DATA WITH MAX 10 POINTS ===
    updateChartData(newTimestamps, newReaders, newWriters) {
        if (!this.clientsChart) return;

        // Add new data
        this.clientsChart.data.labels.push(...newTimestamps);
        this.clientsChart.data.datasets[0].data.push(...newReaders);
        this.clientsChart.data.datasets[1].data.push(...newWriters);

        // Remove oldest data if we exceed maxDataPoints
        if (this.clientsChart.data.labels.length > this.maxDataPoints) {
            const excess = this.clientsChart.data.labels.length - this.maxDataPoints;
            
            // Remove oldest data points
            this.clientsChart.data.labels.splice(0, excess);
            this.clientsChart.data.datasets[0].data.splice(0, excess);
            this.clientsChart.data.datasets[1].data.splice(0, excess);
        }

        // Update the chart
        this.clientsChart.update('none');
        this.updateChartTimestamp('clientsChart');
    }

    // === FETCH LIVE DATA EVERY SECOND ===
    updateRealTimeActiveClients() {
        fetch('/CN_Sessional/cgi-bin/server.cgi/live-active-clients')
            .then(response => {
                if (!response.ok) throw new Error('Network response not ok');
                return response.json();
            })
            .then(data => {
                console.log('Live data:', data);
                if (data.timestamps && data.timestamps.length > 0) {
                    this.updateChartData(data.timestamps, data.active_readers, data.active_writers);
                } else {
                    console.warn('No data received from server');
                    this.showFallbackData();
                }
            })
            .catch(error => {
                console.error('Error fetching live active clients:', error);
                this.showFallbackData();
            });
    }

    	// === FALLBACK IF NO DATA ===
	    showFallbackData() {
	    if (this.clientsChart) {
		this.updateChartTimestamp(
		    'clientsChart',
		    'Unable to load live data'
		);
	    }
	}

    // === UPDATE "LAST UPDATED" TEXT ===
    updateChartTimestamp(chartId, customMessage = null) {
        const now = new Date();
        const timeString = now.toLocaleTimeString();
        const chartContainer = document.getElementById(chartId).closest('.chart-container');
        const updateElement = chartContainer.querySelector('.chart-update');
        if (updateElement) {
            updateElement.textContent = customMessage ? customMessage : `Updated: ${timeString}`;
        }
    }

    // === UPDATE DASHBOARD CARDS ===
    updateDashboard() {
        fetch('/CN_Sessional/cgi-bin/server.cgi/status')
            .then(response => {
                if (!response.ok) throw new Error('Network response not ok');
                return response.json();
            })
            .then(data => {
                if (!data.error) {
                    this.updateStats(data);
                    this.updateCharts(data);
                } else {
                    console.error('Server returned error:', data.error);
                }
            })
            .catch(error => {
		    console.error('Error updating dashboard:', error);
		});
    }
    
       async updatePerformanceChart() {
        try {
            const response = await fetch(
                '/CN_Sessional/cgi-bin/server.cgi/performance'
            );

            if (!response.ok) {
                throw new Error('Performance API request failed');
            }

            const data = await response.json();

            if (this.performanceChart) {
                this.performanceChart.data.datasets[0].data = [
                    data.reader_speed_ms,
                    data.writer_speed_ms,
                    data.reader_throughput_ops_sec,
                    data.writer_throughput_ops_sec,
                    data.reader_reliability_percent,
                    data.writer_reliability_percent
                ];

                this.performanceChart.update('none');
                this.updateChartTimestamp('performanceChart');
            }
        } catch (error) {
            console.error(
                'Error fetching performance metrics:',
                error
            );
        }
    }

    // === UPDATE STATS VALUES ===
    updateStats(data) {
        const liveReaders = document.getElementById('liveReaders');
        const readerTrend = document.getElementById('readerTrend');
        if (liveReaders) liveReaders.textContent = data.activeReaders;
        if (readerTrend) {
            readerTrend.textContent = data.activeReaders > 0 ? 'Active' : 'Idle';
            readerTrend.className = `stat-trend ${data.activeReaders > 0 ? 'positive' : 'neutral'}`;
        }

        const liveWriters = document.getElementById('liveWriters');
        const writerTrend = document.getElementById('writerTrend');
        if (liveWriters) liveWriters.textContent = data.activeWriters;
        if (writerTrend) {
            writerTrend.textContent = data.activeWriters > 0 ? 'Active' : 'Idle';
            writerTrend.className = `stat-trend ${data.activeWriters > 0 ? 'positive' : 'neutral'}`;
        }

        const totalReads = document.getElementById('totalReads');
        const totalWrites = document.getElementById('totalWrites');
        if (totalReads) totalReads.textContent = data.totalReads;
        if (totalWrites) totalWrites.textContent = data.totalWrites;
    }

    // === UPDATE CHARTS (OPERATIONS & PERFORMANCE) ===
    updateCharts(data) {
        if (this.operationsChart) {
            this.operationsChart.data.datasets[0].data = [data.totalReads, data.totalWrites];
            this.operationsChart.update();
        }
    }

    // === START LIVE REFRESH CYCLES ===
    startLiveUpdates() {
	    setInterval(() => {
		this.updateDashboard();
		this.updatePerformanceChart();
	    }, 2000);
	}

    startRealTimeUpdates() {
        setInterval(() => this.updateRealTimeActiveClients(), 1000);
        this.updateRealTimeActiveClients();
    }
}

// === INITIALIZE DASHBOARD ===
document.addEventListener('DOMContentLoaded', () => {
    new Dashboard();
    console.log('Dashboard initialized with local-time aligned real-time tracking');
});
