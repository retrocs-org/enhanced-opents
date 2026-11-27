<?php
declare(strict_types=1);
require __DIR__ . '/campaign.php';

header('Cache-Control: no-store');
header("Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'");
header('X-Content-Type-Options: nosniff');

$campaign = null;
try {
    $campaign = snapshot(database());
} catch (Throwable $error) {
    error_log('OpenTS public web: ' . $error->getMessage());
}
function h(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
$counts = array_count_values(array_column($campaign['territories'] ?? [], 'owner'));
$log = $campaign['history'] ?? [];
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
<meta name="description" content="Follow the Frontier campaign in OpenTS Enhanced World Domination.">
<title>OpenTS Enhanced — Frontier</title>
<link rel="stylesheet" href="assets/site.css">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="top shell">
<a class="brand" href="./"><span class="mark" aria-hidden="true"></span>OpenTS Enhanced</a>
<nav aria-label="Main navigation">
<a href="#frontier">World Domination</a><a href="#history">Match history</a><a href="#play">Get OpenTS Enhanced</a><a href="lobby.php">Private lobby</a>
</nav>
</header>
<main class="shell" id="main">
<section class="hero">
<div>
<p class="eyebrow">World Domination</p>
<h1>Frontier</h1>
<p class="intro">Three territories. Two factions. Every verified victory can change who controls the map.</p>
<a class="button" href="#frontier">View the theater <span aria-hidden="true">↓</span></a>
</div>
<div class="board"<?= $campaign ? ' role="group" aria-label="Frontier territory shortcuts"' : ' aria-hidden="true"' ?>>
<div class="grid"></div><div class="orbit orbit-one"></div><div class="orbit orbit-two"></div><div class="beam"></div>
<?php foreach ($campaign['territories'] ?? [] as $territory): ?>
<a class="pin <?= h($territory['id']) ?> <?= h(strtolower($territory['owner'])) ?>" href="#<?= h($territory['id']) ?>">
<span><?= h($territory['name']) ?></span></a>
<?php endforeach; ?>
</div>
</section>
<section class="section" id="frontier">
<div class="section-head"><div><p class="eyebrow">Campaign</p><h2>Territory control</h2></div>
<?php if ($campaign): ?><div class="score" aria-label="Territory totals">
<span class="gdi">GDI <strong><?= $counts['GDI'] ?? 0 ?></strong></span>
<span class="nod">Nod <strong><?= $counts['Nod'] ?? 0 ?></strong></span>
</div><?php endif; ?></div>
<?php if (!$campaign): ?>
<p class="empty">Territory data is currently unavailable.</p>
<?php else: ?><div class="territories">
<?php foreach ($campaign['territories'] as $territory): ?>
<article id="<?= h($territory['id']) ?>" class="territory <?= h(strtolower($territory['owner'])) ?>">
<div class="territory-art <?= h($territory['id']) ?>" aria-hidden="true"></div>
<div class="card-body"><p class="owner"><span></span><?= h($territory['owner']) ?></p>
<h3><?= h($territory['name']) ?></h3>
<div class="card-foot"><span><?= h($territory['map']) ?></span><span><?= h($battleLabels[$territory['battle_status']] ?? 'Open') ?></span></div></div>
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
<section class="callout" id="play">
<div><p class="eyebrow">The game</p><h2>Get OpenTS Enhanced</h2><p>An open-source engine for Tiberian Sun and Firestorm. Original game data is required.</p></div>
<a class="button" href="https://github.com/OpenTS-Developers/OpenTS/releases">View releases <span aria-hidden="true">↗</span></a>
</section>
</main>
<footer class="shell footer"><span>OpenTS Enhanced</span><div><a href="https://github.com/OpenTS-Developers/OpenTS">Source code</a><a href="api.php?route=theater">Campaign API</a><a href="operator.php">Operator queue</a></div></footer>
</body>
</html>
