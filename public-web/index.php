<?php
declare(strict_types=1);

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
<a class="skip" href="#main">Skip to content</a>
<header class="top shell">
<a class="brand" href="./"><span class="mark" aria-hidden="true"></span>Enhanced OpenTS</a>
<nav aria-label="Main navigation">
<a href="./" aria-current="page">Command Uplink</a>
<a href="world-domination.php">World Domination Tour</a>
<a href="lobby.php">Private lobby</a>
</nav>
</header>
<main class="shell" id="main">
<section class="hero">
<h1>Command Uplink</h1>
<p class="intro">Follow the Frontier campaign or check in for an invited match.</p>
</section>
<section class="section hub-links" aria-label="Multiplayer">
<a class="hub-mode" href="world-domination.php">
<div>
<h2>World Domination Tour</h2>
<p>See who controls Frontier, which territories are contested, and how GDI and Nod are doing this season.</p>
<span class="button">View campaign <span aria-hidden="true">→</span></span>
</div>
</a>
<a class="hub-lobby" href="lobby.php">
<div>
<h2>Private lobby</h2>
<p>Enter your invite code to check in for a match.</p>
<span>Open lobby <span aria-hidden="true">→</span></span>
</div>
</a>
</section>
<section class="callout" id="play">
<div><h2>Get Enhanced OpenTS</h2><p>An open-source engine for Tiberian Sun and Firestorm. Original game data is required.</p></div>
<a class="button" href="https://github.com/OpenTS-Developers/OpenTS/releases">View releases <span aria-hidden="true">↗</span></a>
</section>
</main>
<footer class="shell footer"><span>Enhanced OpenTS</span><div><a href="https://github.com/OpenTS-Developers/OpenTS">Source code</a><a href="api.php?route=theater">Campaign API</a></div></footer>
</body>
</html>
