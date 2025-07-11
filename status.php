<?php
	require_once "db.php";
	db_query("INSERT INTO Humidity (Average) VALUES ('".$_REQUEST['oah']."')") or die($dbcon->error." (".$dbcon->errno.")");
?>