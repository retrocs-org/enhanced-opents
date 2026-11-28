<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Downloads', 'downloads.php', wide: true);
?>
<section class="hero page-art page-art-downloads">
<div class="shell">
<h1>Downloads</h1>
<p class="intro">You’ll need the original Tiberian Sun and Firestorm game data. These download links provide the engine or its source.</p>
</div>
</section>
<div class="shell">
<section class="section overview-grid" aria-label="Builds by platform">
<article class="operator-card"><h2>macOS</h2><p>Enhanced OpenTS provides a native port for Apple silicon and Intel. Check the project releases for packaged builds, or follow the build guide to compile from source.</p>
<p><a href="https://github.com/retrocs-org/enhanced-opents/releases">Enhanced OpenTS releases →</a></p>
<p><a href="https://github.com/retrocs-org/enhanced-opents/blob/macos-port/docs/BUILDING-macOS.md">macOS build guide →</a></p></article>
<article class="operator-card"><h2>Windows</h2><p>Upstream OpenTS publishes Win32 and x64 packages. Extract the package into your Tiberian Sun game directory and run <code>Game.exe</code>.</p>
<p><a href="https://github.com/OpenTS-Developers/OpenTS/releases">OpenTS for Windows →</a></p>
<p><a href="https://opents-developers.github.io/OpenTS/">OpenTS manual →</a></p></article>
</section>
</div>
<?php sitePageEnd(); ?>
