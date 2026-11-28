<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';

$leaderboards = ($_GET['view'] ?? '') === 'leaderboards';
$view = $leaderboards ? 'servers.php?view=leaderboards' : 'servers.php';
$title = $leaderboards ? 'Leaderboards' : 'Public Server Listing';
sitePage($title, 'servers.php', wide: true, view: $view);
?>
<section class="hero page-art page-art-servers">
<div class="shell">
<h1 class="utility-title"><?= $title ?></h1>
<p class="intro"><?= $leaderboards
    ? 'Public server rankings are unavailable until match results are connected.'
    : 'The public server directory is not connected yet.' ?></p>
</div>
</section>
<?php sitePageEnd(); ?>
