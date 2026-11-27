<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Features', 'features.php');
?>
<section class="hero">
<h1>Features</h1>
<p class="intro">The Tiberian Sun and Firestorm campaigns on a reconstructed engine, with modern display options and new tools for players and map makers.</p>
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
<tr><th scope="row">Multiplayer</th><td>Standalone multiplayer matches.</td><td>World Domination Tour’s Frontier campaign tracks territory control and confirmed match results.</td></tr>
</tbody>
</table></div>
</section>
<section class="section" aria-labelledby="timeline-title">
<h2 id="timeline-title">Release timeline</h2>
<ol class="feature-timeline">
<li><time datetime="1999">1999</time><div><h3>Tiberian Sun</h3><p>Westwood Studios releases the original Windows game.</p></div></li>
<li><time datetime="2000">2000</time><div><h3>Firestorm</h3><p>The expansion continues the story with new missions and units.</p></div></li>
<li><time datetime="2026-08-27">27 Aug 2026</time><div><h3>OpenTS 0.1.0</h3><p>The first public OpenTS release brings the reconstructed engine to 32-bit and 64-bit Windows.</p></div></li>
<li><time datetime="2026-11-25">25 Nov 2026</time><div><h3>Enhanced OpenTS macOS port</h3><p>Native Apple silicon and Intel support lands in the development branch.</p></div></li>
</ol>
<p><a href="https://github.com/OpenTS-Developers/OpenTS/blob/main/docs/HISTORY.md">Read the OpenTS project history →</a></p>
</section>
<?php sitePageEnd(); ?>
