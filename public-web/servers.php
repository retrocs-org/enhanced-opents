<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';

header("Content-Security-Policy: default-src 'none'; style-src 'self'; base-uri 'none'; frame-ancestors 'none'");
header('X-Content-Type-Options: nosniff');

$leaderboards = ($_GET['view'] ?? '') === 'leaderboards';
$view = $leaderboards ? 'servers.php?view=leaderboards' : 'servers.php';
$title = $leaderboards ? 'Leaderboards' : 'Public Server Listing';
?>
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= $title ?> · Enhanced OpenTS</title>
<link rel="stylesheet" href="assets/site.css?v=<?= filemtime(__DIR__ . '/assets/site.css') ?>">
</head>
<body>
<?php commandUplinkNav('servers.php', $view); ?>
<main class="shell section" id="main">
<h1 class="utility-title"><?= $title ?></h1>
<p class="intro"><?= $leaderboards
    ? 'Public server rankings are unavailable until match results are connected.'
    : 'The public server directory is not connected yet.' ?></p>
</main>
</body>
</html>
