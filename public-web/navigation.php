<?php
declare(strict_types=1);

function commandUplinkNav(string $current, string $view = ''): void
{
    $pages = [
        'world-domination.php' => 'World Domination Tour',
        'servers.php' => 'Public Server Browser',
        'lobby.php' => 'Private lobby',
    ];
    $sections = match ($current) {
        'world-domination.php' => ['#frontier' => 'Territory map', '#history' => 'Battle history'],
        'servers.php' => ['servers.php' => 'Public Server Listing', 'servers.php?view=leaderboards' => 'Leaderboards'],
        default => [],
    };
    ?>
<a class="skip" href="#main">Skip to content</a>
<header class="shell uplink-header">
<div class="top">
<a class="brand" href="./"><span class="mark" aria-hidden="true"></span>Enhanced OpenTS</a>
<nav aria-label="Main navigation">
<a class="uplink-parent" href="./"<?= $current === 'index.php' ? ' aria-current="page"' : '' ?>>Command Uplink</a>
</nav>
</div>
<nav class="uplink-submenu" aria-label="Command Uplink submenu">
<?php foreach ($pages as $url => $label): ?>
<a href="<?= $url ?>"<?= $current === $url ? ' aria-current="page"' : '' ?>><?= $label ?></a>
<?php endforeach; ?>
</nav>
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
