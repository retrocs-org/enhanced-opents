<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
require __DIR__ . '/releases.php';

$releases = fetchPlatformReleases();
$platforms = [
    'macos' => 'macOS',
    'windows' => 'Windows',
    'linux' => 'Linux',
    'linux-server' => 'Linux dedicated server',
];

function h(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

sitePage('Downloads', 'downloads.php', wide: true);
?>
<section class="hero page-art page-art-downloads">
<div class="shell">
<h1>Downloads</h1>
<p class="intro">Tagged builds from Enhanced OpenTS. To play the client, provide your original Tiberian Sun and Firestorm game data.</p>
</div>
</section>
<div class="shell">
<section class="section" aria-labelledby="platform-builds">
<div class="section-head"><h2 id="platform-builds">Latest builds by platform</h2><a class="feature-link" href="https://github.com/retrocs-org/enhanced-opents/releases">All GitHub releases →</a></div>
<?php if ($releases === null): ?>
<p class="empty" role="status">Release listings are temporarily unavailable. Check GitHub for published builds.</p>
<?php endif; ?>
<div class="overview-grid">
<?php foreach ($platforms as $platform => $label): ?>
<article class="operator-card">
<h3><?= h($label) ?></h3>
<?php if (isset($releases[$platform])): $release = $releases[$platform]; ?>
<p><a href="<?= h($release['url']) ?>"><?= h($release['tag']) ?></a><?= $release['prerelease'] ? ' · Prerelease' : '' ?></p>
<ul>
<?php foreach ($release['assets'] as $asset): ?>
<li><a href="<?= h($asset['url']) ?>"><?= h($asset['name']) ?></a></li>
<?php endforeach; ?>
</ul>
<?php elseif ($releases !== null): ?>
<p>No tagged build has been published for this platform yet.</p>
<?php else: ?>
<p>Release status unavailable.</p>
<?php endif; ?>
</article>
<?php endforeach; ?>
</div>
<p><a class="feature-link" href="source.php">Get the source and build instructions →</a></p>
</section>
</div>
<?php sitePageEnd(); ?>
