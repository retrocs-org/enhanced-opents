<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';

header("Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'");
header('X-Content-Type-Options: nosniff');
?>
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="Find Enhanced OpenTS multiplayer modes and join private matches.">
<title>Command Uplink · Enhanced OpenTS</title>
<link rel="stylesheet" href="assets/site.css?v=<?= filemtime(__DIR__ . '/assets/site.css') ?>">
</head>
<body>
<?php commandUplinkNav('index.php'); ?>
<main class="shell" id="main">
<section class="hero">
<h1>Command Uplink</h1>
<p class="intro">Track the Frontier campaign, browse public servers, or check in for a private match.</p>
</section>
<section class="callout" id="play">
<div><h2>Get Enhanced OpenTS</h2><p>An open-source engine for Tiberian Sun and Firestorm. Original game data is required.</p></div>
<a class="button" href="https://github.com/OpenTS-Developers/OpenTS/releases">View releases <span aria-hidden="true">↗</span></a>
</section>
</main>
<footer class="shell footer"><span>Enhanced OpenTS</span><div><a href="https://github.com/OpenTS-Developers/OpenTS">Source code</a><a href="api.php?route=theater">Campaign API</a></div></footer>
</body>
</html>
