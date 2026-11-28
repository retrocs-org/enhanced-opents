<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Features', 'features.php', wide: true);
?>
<section class="hero page-art page-art-features">
<div class="shell">
<h1>Features</h1>
<p class="intro">From Tiberian Sun and Firestorm to the Frontier multiplayer campaign and a native macOS port.</p>
</div>
</section>
<div class="shell">
<section class="section" aria-labelledby="timeline-title">
<h2 id="timeline-title">Release timeline</h2>
<ol class="feature-timeline">
<li><time datetime="1999">1999</time><div><p class="eyebrow">Original game</p><h3>Tiberian Sun</h3><p>Westwood Studios releases the GDI and Nod campaigns for Windows.</p></div></li>
<li><time datetime="2000">2000</time><div><p class="eyebrow">Expansion</p><h3>Firestorm</h3><p>The story continues with new missions and units.</p></div></li>
<li><time datetime="2026-08-27">27 Aug 2026</time><div><p class="eyebrow">Open-source engine</p><h3>OpenTS 0.1.0</h3><p>The first public release brings the reconstructed engine to 32-bit and 64-bit Windows.</p></div></li>
<li class="feature-timeline-current"><time datetime="2026-11">Nov 2026</time><div><p class="eyebrow">Community fork</p><h3>Enhanced OpenTS</h3><p>Frontier adds a persistent multiplayer campaign, and the native macOS port brings the engine to Apple silicon and Intel Macs.</p><p><a class="feature-link" href="#enhanced">Explore this chapter ↓</a></p></div></li>
</ol>
<p><a class="feature-link" href="https://github.com/OpenTS-Developers/OpenTS/blob/main/docs/HISTORY.md">Read the upstream project history →</a></p>
</section>
<section class="feature-chapter" id="enhanced" aria-labelledby="enhanced-title">
<h2 id="enhanced-title">Built for more ways to play together</h2>
<p class="intro">Enhanced OpenTS adds an ongoing territory campaign, tools for arranging and verifying matches, and more ways to follow multiplayer activity.</p>
<div class="overview-grid">
<article class="operator-card">
<p class="eyebrow">Multiplayer campaign</p>
<h3>Fight for Frontier</h3>
<p>GDI and Nod fight two-player battles across five connected sectors. Capturing a sector moves the front line and opens new targets. An operator-confirmed win transfers control.</p>
<a href="world-domination.php">View the season map →</a>
</article>
<article class="operator-card">
<p class="eyebrow">Multiplayer hosting</p>
<h3>Arrange and verify matches</h3>
<p>Hosts reserve a territory and assign one seat per faction. Invited players check in with private codes. The host confirms both connections, runs the assigned scenario, and submits the result for review. Draws, aborted matches, and expired reservations leave the map unchanged.</p>
</article>
<article class="operator-card">
<p class="eyebrow">Command Uplink</p>
<h3>Follow the season</h3>
<p>The public map shows live ownership, active battles, faction wins, and recent results. The in-game Frontier menu can show territories and recent results when configured.</p>
<a href="command-uplink.php">Open Command Uplink →</a>
</article>
<article class="operator-card">
<p class="eyebrow">Multiplayer in development</p>
<h3>Public servers and leaderboards</h3>
<p>Public server discovery and player rankings are the next areas of focus. Command Uplink already has separate views for both. Live listings and rankings will become available when the server directory and match results are connected.</p>
<a href="servers.php">Visit the Public Server Browser →</a>
</article>
<article class="operator-card">
<p class="eyebrow">Map editor</p>
<h3>Build your own multiplayer maps</h3>
<p>Create temperate or snow maps in the in-game editor, set up player starting positions, and save or export scenarios for multiplayer.</p>
</article>
<article class="operator-card">
<p class="eyebrow">macOS port</p>
<h3>Play natively on Mac</h3>
<p>The port targets Apple silicon and Intel on macOS 13.3 or newer, with Metal rendering, window resizing, and fullscreen support. The build tooling supports signing and notarization for distribution. Bring your original Tiberian Sun and Firestorm data.</p>
<a href="downloads.php">See macOS build options →</a>
</article>
</div>
</section>
</div>
<?php sitePageEnd(); ?>
