<?php
declare(strict_types=1);
require __DIR__ . '/campaign.php';

header('Cache-Control: no-store');
header("Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'");
header('X-Content-Type-Options: nosniff');

$campaign = null;
$wins = null;
try {
    $db = database();
    $campaign = snapshot($db);
    $wins = $db->query("
        SELECT winner, COUNT(*) AS victories FROM wdt_battle
        WHERE status = 'won' AND finished_at IS NOT NULL AND winner IS NOT NULL
        GROUP BY winner
    ")->fetchAll(PDO::FETCH_KEY_PAIR);
} catch (Throwable $error) {
    error_log('OpenTS public web: ' . $error->getMessage());
}
function h(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
$territories = $campaign['territories'] ?? [];
$counts = array_count_values(array_column($territories, 'owner'));
$mappedTerritories = ['river-crossing', 'central-basin', 'ridge-line'];
$log = $campaign['history'] ?? [];
$activeBattles = count(array_filter(array_column($territories, 'battle_id')));
$battleLabels = [
    'reserved' => 'Reserved',
    'in_progress' => 'In progress',
    'review' => 'Awaiting review',
    'won' => 'Completed',
    'draw' => 'Draw',
    'aborted' => 'Aborted',
    'expired' => 'Expired',
];
?>
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="Follow territory control and active battles in the Enhanced OpenTS World Domination Tour.">
<title>World Domination Tour · Enhanced OpenTS</title>
<link rel="stylesheet" href="assets/site.css?v=<?= filemtime(__DIR__ . '/assets/site.css') ?>">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="top shell">
<a class="brand" href="./"><span class="mark" aria-hidden="true"></span>Enhanced OpenTS</a>
<nav aria-label="Main navigation">
<a href="./">Command Uplink</a><a href="#frontier">Territory map</a><a href="#history">Battle history</a><a href="lobby.php">Private lobby</a>
</nav>
</header>
<main class="shell" id="main">
<section class="hero">
<p class="eyebrow">World Domination Tour</p>
<h1><?= h($campaign['name'] ?? 'Frontier') ?></h1>
<p class="intro">GDI and Nod fight for control of Frontier. Only an operator-confirmed win changes territory ownership.</p>
<?php if ($campaign): ?>
<dl class="season-stats" aria-label="Frontier season statistics">
<div><dt>Territories</dt><dd><?= count($territories) ?></dd></div>
<div><dt>Active battles</dt><dd><?= $activeBattles ?></dd></div>
<?php if ($wins !== null): ?>
<div><dt>GDI wins</dt><dd><?= (int) ($wins['GDI'] ?? 0) ?></dd></div>
<div><dt>Nod wins</dt><dd><?= (int) ($wins['Nod'] ?? 0) ?></dd></div>
<?php endif; ?>
</dl>
<?php endif; ?>
</section>
<section class="section" id="frontier">
<div class="section-head"><div><p class="eyebrow">Current season</p><h2>Territory control</h2></div>
<?php if ($territories): ?><div class="score" aria-label="Territories by owner">
<span class="gdi">GDI <strong><?= $counts['GDI'] ?? 0 ?></strong></span>
<span class="nod">Nod <strong><?= $counts['Nod'] ?? 0 ?></strong></span>
<span class="neutral">Neutral <strong><?= $counts['Neutral'] ?? 0 ?></strong></span>
</div><?php endif; ?></div>
<?php if (!$territories): ?>
<p class="empty"><?= $campaign ? 'No territories are configured for this season.' : 'Territory data is currently unavailable.' ?></p>
<?php else: ?>
<div class="season-map" role="group" aria-label="Frontier season map">
<?php foreach ($territories as $territory): ?>
<?php if (!in_array($territory['id'], $mappedTerritories, true)) { continue; } ?>
<div class="map-region <?= h($territory['id']) ?> <?= h(strtolower($territory['owner'])) ?>">
<div class="map-land" aria-hidden="true"></div>
<a class="map-marker" href="#<?= h($territory['id']) ?>">
<strong><?= h($territory['name']) ?></strong>
<span class="map-owner"><?= h($territory['owner']) ?></span>
<?php if ($territory['battle_status'] !== null): ?>
<span class="map-battle"><?= h($battleLabels[$territory['battle_status']] ?? $territory['battle_status']) ?></span>
<?php endif; ?>
</a>
</div>
<?php endforeach; ?>
</div>
<div class="territories">
<?php foreach ($territories as $territory): ?>
<article id="<?= h($territory['id']) ?>" class="territory <?= h(strtolower($territory['owner'])) ?>">
<div class="territory-art <?= h($territory['id']) ?>" aria-hidden="true"></div>
<div class="card-body"><p class="owner"><span></span><?= h($territory['owner']) ?></p>
<h3><?= h($territory['name']) ?></h3>
<div class="card-foot"><span><?= h($territory['map']) ?></span><span><?= h($battleLabels[$territory['battle_status'] ?? ''] ?? 'Open') ?></span></div></div>
</article>
<?php endforeach; ?>
</div><?php endif; ?>
</section>
<section class="section" id="history">
<div class="section-head"><div><p class="eyebrow">Campaign record</p><h2>Recent battles</h2></div></div>
<?php if (!$log): ?>
<p class="empty"><?= $campaign ? 'No completed battles yet.' : 'Battle history is currently unavailable.' ?></p>
<?php else: ?>
<div class="table-wrap"><table><thead><tr><th scope="col">Territory</th><th scope="col">Result</th><th scope="col">Details</th><th scope="col">Finished (UTC)</th></tr></thead><tbody>
<?php foreach ($log as $battle): ?>
<tr><td><?= h($battle['territory']) ?></td><td><?= h($battle['winner'] ? $battle['winner'] . ' victory' : ($battleLabels[$battle['status']] ?? $battle['status'])) ?></td>
<td><?= h($battle['reason'] ?? '—') ?></td><td><time datetime="<?= gmdate('c', (int) $battle['finished_at']) ?>"><?= gmdate('j M Y · H:i', (int) $battle['finished_at']) ?></time></td></tr>
<?php endforeach; ?>
</tbody></table></div><?php endif; ?>
</section>
</main>
<footer class="shell footer"><span>Enhanced OpenTS</span><div><a href="https://github.com/OpenTS-Developers/OpenTS">Source code</a><a href="api.php?route=theater">Campaign API</a><a href="operator.php">Operator queue</a></div></footer>
</body>
</html>
