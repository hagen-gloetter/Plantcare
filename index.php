<html>
<head>
	<script src="https://cdnjs.cloudflare.com/ajax/libs/moment.js/2.29.1/moment.min.js"></script>
	<script src="https://cdn.jsdelivr.net/npm/chart.js@2.9.4/dist/Chart.min.js"></script>
</head>
<body style="background:#020;color:#0f0">
	<div class="container">
		<canvas id="examChart"></canvas>
	</div>
	<script languange="javascript">
		const TICKS = true;
		new Chart(document.getElementById("examChart").getContext("2d"), {
			type: 'line',
			options: {
				scales: {
					xAxes: [{
						color: 'rgba(0, 255, 0, 1)',
						type: 'time',
						time: {
		                    unit: 'day',
		                    displayFormats: {
		                        day: 'MMM D',
		                        hour: 'HH'
		                    },
		                    stepSize: 1
		                },
						display: true,
						gridLines: {
							color: 'rgba(0, 100, 0, 1)',
							zeroLineColor: 'rgba(0, 100, 0, 1)'
						},
						scaleLabel: {
							display: true,
							labelString: 'Date',
						},
					}],
					yAxes: [{
						display: true,
						gridLines: {
							color: 'rgba(0, 100, 0, 1)'
						},
						scaleLabel: {
							display: true,
							labelString: 'Humidity in %'
						},
						ticks: {
		                    stepSize: 1,
		                    precision: 0
		                }
					}]
				},
			},
			data: {
				datasets: [{
					tension: .3,
					fill: false,
					pointRadius: 1.5,
					label: 'Humidity Values',
					data: [
<?php
	require_once "db.php";
	foreach (db_query("SELECT Date, Average FROM Humidity ORDER BY Id", true)->fetch_all() AS $x) { ?>
						{t: '<?=$x[0]?>',y: <?=$x[1]?>},
<?	} ?>
					],
					borderColor: 'rgba(0, 255, 0, 1)',
					borderWidth: 2
				}]
			}
		});
	</script>
</body>
</html>
