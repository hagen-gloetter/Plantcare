<?php
	/**
	 * @brief Receives humidity telemetry from the ESP32 and writes it to the database.
	 *
	 * GET parameters: oah=<float> (overall average humidity), t1=<int> (upper threshold), t2=<int> (lower threshold)
	 * Security: all inputs are bound via prepared statements — no SQL injection possible.
	 */
	require_once "db.php";

	// BUG-01 fix: use prepared statements to prevent SQL injection.
	$oah = (float)($_REQUEST['oah'] ?? 0);
	$stmt = $dbcon->prepare("INSERT INTO Humidity (Average) VALUES (?)");
	$stmt->bind_param("d", $oah);
	$stmt->execute() or die($dbcon->error . " (" . $dbcon->errno . ")");
	$stmt->close();

	if (!empty($_REQUEST['t1'])) {
		$t1 = (int)$_REQUEST['t1'];
		$stmt = $dbcon->prepare("UPDATE Config SET Value=? WHERE Id='Threshold1'");
		$stmt->bind_param("i", $t1);
		$stmt->execute() or die($dbcon->error . " (" . $dbcon->errno . ")");
		$stmt->close();
	}
	if (!empty($_REQUEST['t2'])) {
		$t2 = (int)$_REQUEST['t2'];
		$stmt = $dbcon->prepare("UPDATE Config SET Value=? WHERE Id='Threshold2'");
		$stmt->bind_param("i", $t2);
		$stmt->execute() or die($dbcon->error . " (" . $dbcon->errno . ")");
		$stmt->close();
	}
?>