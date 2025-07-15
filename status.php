<?php
	require_once "db.php";
	db_query("INSERT INTO Humidity (Average) VALUES ('".$_REQUEST['oah']."')") or die($dbcon->error." (".$dbcon->errno.")");
	if ($_REQUEST['t1'])
		db_query("UPDATE Config SET Value='".$_REQUEST['t1']."' WHERE Id='Threshold1'") or die($dbcon->error." (".$dbcon->errno.")");
	if ($_REQUEST['t2'])
		db_query("UPDATE Config SET Value='".$_REQUEST['t2']."' WHERE Id='Threshold2'") or die($dbcon->error." (".$dbcon->errno.")");
?>