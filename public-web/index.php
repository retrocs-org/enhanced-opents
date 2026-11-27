<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Overview', './');
?>
<section class="hero overview-hero">
<p class="eyebrow">Command &amp; Conquer · Tiberian Sun</p>
<h1>Enhanced OpenTS</h1>
<p class="intro">Play Tiberian Sun and Firestorm on an open-source engine. Choose GDI or Nod, build a base, harvest Tiberium, and fight across campaigns and skirmishes.</p>
<a class="button" href="downloads.php">Get the game <span aria-hidden="true">→</span></a>
</section>
<section class="section overview-grid" aria-label="Explore Enhanced OpenTS">
<article class="operator-card"><h2>Campaigns and skirmish</h2><p>Play the original missions or set up a custom match against the AI. Enhanced OpenTS uses your original game data.</p><a href="features.php">Explore features and history →</a></article>
<article class="operator-card"><h2>Multiplayer</h2><p>Follow territory control, active battles and faction scores in the Frontier season of World Domination Tour.</p><a href="command-uplink.php">Open Command Uplink →</a></article>
</section>
<?php sitePageEnd(); ?>
