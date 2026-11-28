<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
require __DIR__ . '/campaign.php';

header('Cache-Control: no-store');
$campaign = null;
try {
    $campaign = snapshot(database());
} catch (Throwable $error) {
    error_log('Command Uplink campaign snapshot unavailable [' . $error->getCode() . ']');
}

function h(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

$territories = $campaign['territories'] ?? [];
$owners = array_count_values(array_column($territories, 'owner'));
$activeBattles = count(array_filter(array_column($territories, 'battle_id')));
$recent = array_slice($campaign['history'] ?? [], 0, 4);
sitePage('Command Uplink', 'command-uplink.php');
?>
<section class="hero uplink-intro page-art page-art-uplink">
<p class="eyebrow">Multiplayer hub</p>
<h1>Command Uplink</h1>
<p class="intro">Follow the campaign, see who holds territory, or find multiplayer servers.</p>
</section>
<section class="uplink-feature" aria-labelledby="uplink-campaign">
<div>
<p class="eyebrow">Current campaign</p>
<h2 id="uplink-campaign"><?= h($campaign['name'] ?? 'Frontier') ?></h2>
<p>GDI and Nod fight for control of Frontier. Follow the front line and see which territories are open to challenge.</p>
<a class="button" href="world-domination.php">View territory map <span aria-hidden="true">→</span></a>
</div>
</section>
<?php if ($campaign !== null): ?>
<dl class="uplink-stats" aria-label="Frontier campaign status">
<div class="gdi"><dt>GDI territories</dt><dd><?= $owners['GDI'] ?? 0 ?></dd></div>
<div class="nod"><dt>Nod territories</dt><dd><?= $owners['Nod'] ?? 0 ?></dd></div>
<div class="neutral"><dt>Neutral territories</dt><dd><?= $owners['Neutral'] ?? 0 ?></dd></div>
<div><dt>Active battles</dt><dd><?= $activeBattles ?></dd></div>
</dl>
<?php else: ?>
<p class="empty uplink-unavailable">Live campaign status is temporarily unavailable.</p>
<?php endif; ?>
<div class="section uplink-activity">
<section aria-labelledby="uplink-battles">
<h2 id="uplink-battles">Recent battles</h2>
<?php if ($recent): ?>
<ul class="uplink-results">
<?php foreach ($recent as $battle): ?>
<li>
<strong><?= h($battle['territory']) ?></strong>
<span><?= h($battle['winner'] ? $battle['winner'] . ' victory' : ucfirst($battle['status'])) ?></span>
<time datetime="<?= gmdate('c', (int) $battle['finished_at']) ?>"><?= gmdate('j M · H:i', (int) $battle['finished_at']) ?> UTC</time>
</li>
<?php endforeach; ?>
</ul>
<?php elseif ($campaign !== null): ?>
<p>No completed battles yet.</p>
<?php else: ?>
<p>Battle results are temporarily unavailable.</p>
<?php endif; ?>
<a class="uplink-link" href="world-domination.php#history">Full battle history →</a>
</section>
<section aria-labelledby="uplink-servers">
<h2 id="uplink-servers">Public servers</h2>
<p>The public server directory is awaiting integration.</p>
<a class="uplink-link" href="servers.php">Public Server Listing →</a>
<a class="uplink-link" href="servers.php?view=leaderboards">Leaderboards →</a>
</section>
</div>
<?php sitePageEnd(); ?>
