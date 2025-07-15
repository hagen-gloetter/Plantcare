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
<?	require_once "db.php";
	$row = db_query("SELECT c1.Value AS Upper, c2.Value AS Lower FROM Config c1 INNER JOIN Config c2 ON c2.Id='Threshold2' WHERE c1.Id='Threshold1'")->fetch_assoc();
	$row2 = db_query("SELECT MIN(Date) AS Start, MAX(Date) AS End FROM Humidity WHERE Date > SUBDATE(NOW(), 8) ORDER BY Id")->fetch_assoc();
?>
				datasets: [{
						label: 'Upper Threshold',
						data: [
							{t:'<?=$row2['Start']?>', y:<?=$row['Upper']?>},
							{t:'<?=$row2['End']?>', y:<?=$row['Upper']?>}
						],
						borderColor: 'rgba(255, 0, 0, .5)',
					}, {
						label: 'Lower Threshold',
						data: [
							{t:'<?=$row2['Start']?>', y:<?=$row['Lower']?>},
							{t:'<?=$row2['End']?>', y:<?=$row['Lower']?>}
						],
						borderColor: 'rgba(0, 0, 255, .5)',
					}, {
					tension: .3,
					pointRadius: 1.5,
					label: 'Humidity Values',
					data: [
<?	foreach (db_query("SELECT Date, Average FROM Humidity WHERE Date > SUBDATE(NOW(), 8) ORDER BY Id")->fetch_all() AS $x) { ?>
						{t:'<?=$x[0]?>', y:<?=$x[1]?>},
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
