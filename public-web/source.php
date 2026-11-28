<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
sitePage('Source', 'source.php', wide: true);
?>
<section class="hero page-art page-art-downloads">
<div class="shell">
<h1>Source</h1>
<p class="intro">Build the Enhanced OpenTS client from the project repository, or set up the Frontier match-host tools.</p>
</div>
</section>
<div class="shell">
<section class="section" aria-labelledby="source-checkout">
<h2 id="source-checkout">Get the source</h2>
<p><a class="feature-link" href="https://github.com/retrocs-org/enhanced-opents">Enhanced OpenTS on GitHub →</a></p>
<pre class="source-commands"><code>git clone --branch macos-port --recurse-submodules https://github.com/retrocs-org/enhanced-opents.git
cd enhanced-opents</code></pre>
</section>
<section class="section overview-grid" aria-label="Client build instructions">
<article class="operator-card"><h3>macOS client</h3>
<p>Install the Xcode command-line tools and CMake 3.23 or newer. Run the build on the target Mac architecture.</p>
<pre class="source-commands"><code>tools/macos/build.sh</code></pre>
<a href="https://github.com/retrocs-org/enhanced-opents/blob/macos-port/docs/BUILDING-macOS.md">macOS build guide →</a>
</article>
<article class="operator-card"><h3>Windows client</h3>
<p>Use Visual Studio 2022 with Desktop development with C++ and CMake 3.23 or newer. Run from PowerShell. Use <code>Win32</code> with a separate build directory for 32-bit.</p>
<pre class="source-commands"><code>cmake -S . -B build/windows-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/windows-x64 --config Release</code></pre>
<a href="https://github.com/retrocs-org/enhanced-opents/blob/macos-port/docs/BUILDING.md">Windows build guide →</a>
</article>
<article class="operator-card"><h3>Linux client</h3>
<p>The 64-bit POSIX build is experimental on Linux. Use CMake 3.23 or newer, Clang, and a C++ standard library with C++20 format support. Gameplay still needs verification.</p>
<pre class="source-commands"><code>cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/linux --parallel</code></pre>
<a href="https://github.com/retrocs-org/enhanced-opents/blob/macos-port/docs/BUILDING-macOS.md#limitations">POSIX build notes →</a>
</article>
</section>
<section class="section" aria-labelledby="source-server">
<h2 id="source-server">Linux dedicated server and Frontier hosting</h2>
<p>The repository has no standalone dedicated-server build target. The available Frontier tools run a normal OpenTS client as the match host and report results to the website.</p>
<p>Install Python 3.9 or newer, the Python protobuf runtime, and <code>protoc</code>. From the repository root, generate bindings and check the runner’s options.</p>
<pre class="source-commands"><code>mkdir -p build/frontier-proto
protoc -I docs/launch --python_out=build/frontier-proto docs/launch/launch.proto
PYTHONPATH="$PWD/build/frontier-proto" python3 tools/world-domination/run.py --help</code></pre>
<p><a class="feature-link" href="https://github.com/retrocs-org/enhanced-opents/blob/macos-port/tools/world-domination/README.md#match-host-runner">Frontier match-host setup →</a></p>
</section>
</div>
<?php sitePageEnd(); ?>
