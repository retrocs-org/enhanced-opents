<?php
declare(strict_types=1);

function sitePage(string $title, string $current, bool $wide = false): void
{
    header("Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    header('X-Content-Type-Options: nosniff');
    ?>
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= htmlspecialchars($title, ENT_QUOTES, 'UTF-8') ?> · Enhanced OpenTS</title>
<link rel="stylesheet" href="assets/site.css?v=<?= filemtime(__DIR__ . '/assets/site.css') ?>">
</head>
<body>
<?php commandUplinkNav($current); ?>
<main<?= $wide ? '' : ' class="shell"' ?> id="main">
    <?php
}

function sitePageEnd(): void
{
    ?>
</main>
<footer class="shell footer"><span>Enhanced OpenTS</span><a href="https://github.com/retrocs-org/enhanced-opents">Source code</a></footer>
</body>
</html>
    <?php
}

function commandUplinkNav(string $current, string $view = ''): void
{
    $pages = [
        'world-domination.php' => 'World Domination Tour',
        'servers.php' => 'Public Server Browser',
    ];
    $main = [
        './' => 'Overview',
        'features.php' => 'Features',
        'command-uplink.php' => 'Command Uplink',
        'downloads.php' => 'Downloads',
        'sign-in.php' => 'Sign in',
    ];
    $showModes = $current === 'command-uplink.php' || isset($pages[$current]);
    $sections = match ($current) {
        'world-domination.php' => ['#frontier' => 'Territory map', '#history' => 'Battle history'],
        'servers.php' => ['servers.php' => 'Public Server Listing', 'servers.php?view=leaderboards' => 'Leaderboards'],
        default => [],
    };
    ?>
<a class="skip" href="#main">Skip to content</a>
<header class="shell site-header">
<div class="top">
<a class="brand" href="./"><img class="brand-emblem" src="assets/opents-faction-emblem.webp" width="42" height="42" alt=""><span>Enhanced OpenTS</span></a>
<nav aria-label="Main navigation">
<?php foreach ($main as $url => $label): ?>
<a href="<?= $url ?>"<?= $current === $url ? ' aria-current="page"' : '' ?><?= $showModes && $url === 'command-uplink.php' ? ' class="uplink-parent"' : '' ?>><?= $label ?></a>
<?php endforeach; ?>
</nav>
</div>
<?php if ($showModes): ?>
<nav class="uplink-submenu" aria-label="Command Uplink submenu">
<?php foreach ($pages as $url => $label): ?>
<a href="<?= $url ?>"<?= $current === $url ? ' aria-current="page"' : '' ?>><?= $label ?></a>
<?php endforeach; ?>
</nav>
<?php endif; ?>
<?php if ($sections): ?>
<nav class="uplink-sections" aria-label="<?= $pages[$current] ?> sections">
<?php foreach ($sections as $url => $label): ?>
<a href="<?= $url ?>"<?= $view === $url ? ' aria-current="page"' : '' ?>><?= $label ?></a>
<?php endforeach; ?>
</nav>
<?php endif; ?>
</header>
    <?php
}
