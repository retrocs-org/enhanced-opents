<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Command Uplink', 'command-uplink.php');
?>
<section class="hero">
<h1>Command Uplink</h1>
<p class="intro">Track the Frontier campaign, check faction scores, or browse public multiplayer servers.</p>
</section>
<p class="section"><a href="api.php?route=theater">Campaign API</a></p>
<?php sitePageEnd(); ?>
