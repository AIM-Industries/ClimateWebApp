<%@page import="aimi.store.JsonStore"%>
<%@page contentType="text/html; charset=UTF-8" pageEncoding="UTF-8"%>
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <title>Climate Charts</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns"></script>
  <style>
    body { font-family: Arial, Helvetica, sans-serif; margin: 24px; background:#fafafa; }
    .card { background:#fff; border:1px solid #eee; border-radius:12px; padding:18px; margin:18px 0; box-shadow:0 2px 10px rgba(0,0,0,0.04); }
    h2 { margin:0 0 12px 0; font-weight:600; }
    canvas { width:100%; height:420px; }
    .meta { color:#777; font-size:12px; margin-bottom:8px; }
  </style>
</head>
<body>
<%
  JsonStore store = new JsonStore("data.json");
  String chartJson = store.loadJson(); // temps already converted to °F in JsonStore
%>

<div class="card">
  <h2>Temperature (°F)</h2>
  <div class="meta">Each line = deviceId</div>
  <canvas id="tempChart"></canvas>
</div>

<div class="card">
  <h2>Humidity (%)</h2>
  <div class="meta">Each line = deviceId</div>
  <canvas id="humChart"></canvas>
</div>

<script>
  // Server payload
  const payload = <%= chartJson %>; // { labels:[ISO...], datasets:[{label,temps,hums}, ...] }
  const labels = payload.labels || [];
  const deviceDatasets = payload.datasets || [];

  // Datasets
  const tempDatasets = deviceDatasets.map(ds => ({
    label: `${ds.label} (°F)`,                 // <-- show °F
    data: (ds.temps || []).map(Number),
    spanGaps: true,
    borderWidth: 2,
    pointRadius: 2,
    tension: 0.25
  }));

  const humDatasets = deviceDatasets.map(ds => ({
    label: `${ds.label} (%)`,
    data: (ds.hums || []).map(Number),
    spanGaps: true,
    borderWidth: 2,
    pointRadius: 2,
    borderDash: [4,4],
    tension: 0.25
  }));

  // X-axis: hours with am/pm
  const xScale = {
    type: 'time',
    time: { unit: 'hour', displayFormats: { hour: 'h a' } },
    ticks: { autoSkip: true, maxTicksLimit: 10 },
    grid: { display: false },
    title: { display: true, text: 'Time' }
  };

  // Common opts with 1-decimal formatting for °F
  const commonOpts = {
    responsive: true,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: { labels: { usePointStyle: true } },
      tooltip: {
        callbacks: {
          // Pretty tooltip title: "Aug 17, 8:41 PM"
          title: (items) => {
            if (!items.length) return '';
            const raw = labels[items[0].dataIndex];
            const d = new Date(raw);
            return d.toLocaleString('en-US', {
              month: 'short', day: 'numeric',
              hour: 'numeric', minute: '2-digit',
              hour12: true
            });
          },
          // 1 decimal for temperature values
          label: (ctx) => {
            const v = typeof ctx.parsed.y === 'number' ? ctx.parsed.y : Number(ctx.formattedValue);
            const series = ctx.dataset.label || 'Value';
            return `${series}: ${v.toFixed(1)}`;
          }
        }
      }
    },
    scales: { x: xScale }
  };

  // Temperature chart (°F + 1 decimal ticks)
  new Chart(document.getElementById('tempChart').getContext('2d'), {
    type: 'line',
    data: { labels, datasets: tempDatasets },
    options: {
      ...commonOpts,
      scales: {
        ...commonOpts.scales,
        y: {
          title: { display: true, text: '°F' },
          ticks: {
            callback: (val) => Number(val).toFixed(1) // 1 decimal on axis
          }
        }
      },
      plugins: { ...commonOpts.plugins, title: { display: true, text: 'Temperature Over Time' } }
    }
  });

  // Humidity chart (leave as is)
  new Chart(document.getElementById('humChart').getContext('2d'), {
    type: 'line',
    data: { labels, datasets: humDatasets },
    options: {
      ...commonOpts,
      scales: {
        ...commonOpts.scales,
        y: { title: { display: true, text: '%' }, suggestedMin: 0, suggestedMax: 100 }
      },
      plugins: { ...commonOpts.plugins, title: { display: true, text: 'Humidity Over Time' } }
    }
  });
</script>
</body>
</html>
