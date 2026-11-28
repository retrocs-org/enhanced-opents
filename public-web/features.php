<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Features', 'features.php');
?>
<section class="hero page-art page-art-features">
<h1>Features</h1>
<p class="intro">From Tiberian Sun and Firestorm to the Frontier multiplayer campaign and a native macOS port.</p>
</section>
<section class="section" aria-labelledby="timeline-title">
<h2 id="timeline-title">Release timeline</h2>
<ol class="feature-timeline">
<li><time datetime="1999">1999</time><div><p class="eyebrow">Original game</p><h3>Tiberian Sun</h3><p>Westwood Studios releases the GDI and Nod campaigns for Windows.</p></div></li>
<li><time datetime="2000">2000</time><div><p class="eyebrow">Expansion</p><h3>Firestorm</h3><p>The story continues with new missions and units.</p></div></li>
<li><time datetime="2026-08-27">27 Aug 2026</time><div><p class="eyebrow">Open-source engine</p><h3>OpenTS 0.1.0</h3><p>The first public release brings the reconstructed engine to 32-bit and 64-bit Windows.</p></div></li>
<li class="feature-timeline-current"><time datetime="2026-11">Nov 2026</time><div><p class="eyebrow">Community fork</p><h3>Enhanced OpenTS</h3><p>Frontier adds a persistent multiplayer campaign, and the native macOS port brings the engine to Apple silicon and Intel Macs.</p><p><a class="feature-link" href="#enhanced">Explore this chapter ↓</a></p></div></li>
</ol>
<section class="feature-chapter" id="enhanced" aria-labelledby="enhanced-title">
<h2 id="enhanced-title">Enhanced OpenTS</h2>
<div class="overview-grid">
<article class="operator-card">
<p class="eyebrow">Multiplayer campaign</p>
<h3>Fight for Frontier</h3>
<p>GDI and Nod compete across five Frontier sectors in two-player battles. A territory stays reserved while a match is being arranged, played, or reviewed. Only an operator-confirmed win changes ownership.</p>
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
<p>The public map shows live ownership, active battles, faction wins, and recent results. The in-game Frontier menu can show territories and recent results when configured. Public Server Listing and Leaderboards have dedicated views awaiting a server directory and match data.</p>
<a href="command-uplink.php">Open Command Uplink →</a>
</article>
<article class="operator-card">
<p class="eyebrow">macOS port</p>
<h3>Play natively on Mac</h3>
<p>The port targets Apple silicon and Intel on macOS 13.3 or newer, with Metal rendering, window resizing, and fullscreen support. The build tooling supports signing and notarization for distribution. Bring your original Tiberian Sun and Firestorm data.</p>
<a href="downloads.php">See macOS build options →</a>
</article>
</div>
</section>
<p><a class="feature-link" href="https://github.com/OpenTS-Developers/OpenTS/blob/main/docs/HISTORY.md">Read the upstream project history →</a></p>
</section>
<section class="section">
<h2>What’s changed</h2>
<div class="table-wrap feature-comparison"><table>
<thead><tr><th scope="col">Area</th><th scope="col">Original games</th><th scope="col">Enhanced OpenTS</th></tr></thead>
<tbody>
<tr><th scope="row">Campaigns</th><td>GDI and Nod campaigns in Tiberian Sun, with new missions and units in Firestorm.</td><td>Uses the original game data for campaigns and skirmish.</td></tr>
<tr><th scope="row">Platforms</th><td>Originally released for Windows.</td><td>OpenTS provides Windows builds. Enhanced OpenTS adds a native macOS port for Apple silicon and Intel.</td></tr>
<tr><th scope="row">Display</th><td>Display modes from the retail Windows release.</td><td>Modern resolutions, scaling controls and Metal rendering on macOS.</td></tr>
<tr><th scope="row">Maps</th><td>Scenario maps shipped with the game and expansion.</td><td>An in-game editor can create or edit maps and save multiplayer scenarios.</td></tr>
<tr><th scope="row">Multiplayer</th><td>Standalone multiplayer matches.</td><td>The Frontier campaign tracks territory control and confirmed match results.</td></tr>
</tbody>
</table></div>
</section>
<?php sitePageEnd(); ?>
