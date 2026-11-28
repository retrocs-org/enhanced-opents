<?php
declare(strict_types=1);

const RELEASE_REPOSITORY = 'retrocs-org/enhanced-opents';

function releasePlatform(string $name): ?string
{
    if (!preg_match('/\.(?:zip|dmg|pkg|exe|msi|appimage|deb|rpm|tar\.(?:gz|xz|zst))\z/i', $name)
        || preg_match('/(?:^|[._-])(?:source|sources|src|debug|symbols)(?:[._-]|$)/i', $name)) {
        return null;
    }
    $linux = preg_match('/(?:^|[._-])linux(?:[._-]|$)/i', $name);
    if (preg_match('/(?:^|[._-])(?:dedicated|server)(?:[._-]|$)/i', $name)) {
        return $linux ? 'linux-server' : null;
    }
    return match (true) {
        $linux === 1 => 'linux',
        preg_match('/(?:^|[._-])(?:macos|darwin|osx)(?:[._-]|$)/i', $name) === 1 => 'macos',
        preg_match('/(?:^|[._-])(?:windows|win32|win64|win)(?:[._-]|$)/i', $name) === 1 => 'windows',
        preg_match('/\.(?:dmg|pkg)\z/i', $name) === 1 => 'macos',
        preg_match('/\AOpenTS-.+-x64\.zip\z/i', $name) === 1 => 'windows',
        preg_match('/\.(?:exe|msi)\z/i', $name) === 1 => 'windows',
        preg_match('/\.appimage\z/i', $name) === 1 => 'linux',
        default => null,
    };
}

function latestPlatformReleases(array $releases): array
{
    $platforms = [];
    $prefix = 'https://github.com/' . RELEASE_REPOSITORY . '/releases/';
    foreach ($releases as $release) {
        $tag = $release['tag_name'] ?? '';
        $date = $release['published_at'] ?? '';
        $url = $release['html_url'] ?? '';
        if (($release['draft'] ?? true) || !is_string($tag) || $tag === ''
            || !is_string($date) || $date === '' || !is_string($url) || !str_starts_with($url, $prefix . 'tag/')) {
            continue;
        }
        $byPlatform = [];
        foreach ($release['assets'] ?? [] as $asset) {
            $name = $asset['name'] ?? '';
            $download = $asset['browser_download_url'] ?? '';
            if (!is_string($name) || !is_string($download) || !str_starts_with($download, $prefix . 'download/')) {
                continue;
            }
            $platform = releasePlatform($name);
            if ($platform !== null) {
                $byPlatform[$platform][] = ['name' => $name, 'url' => $download];
            }
        }
        foreach ($byPlatform as $platform => $assets) {
            if (!isset($platforms[$platform]) || $date > $platforms[$platform]['date']) {
                $platforms[$platform] = [
                    'tag' => $tag,
                    'date' => $date,
                    'url' => $url,
                    'prerelease' => (bool) ($release['prerelease'] ?? false),
                    'assets' => $assets,
                ];
            }
        }
    }
    return $platforms;
}

function fetchPlatformReleases(): ?array
{
    $cacheKey = 'enhanced-opents-platform-releases-v1';
    $cache = function_exists('apcu_enabled') && apcu_enabled();
    if ($cache) {
        $cached = apcu_fetch($cacheKey, $found);
        if ($found) {
            return $cached;
        }
    }
    $context = stream_context_create(['http' => [
        'timeout' => 3,
        'follow_location' => 0,
        'header' => "Accept: application/vnd.github+json\r\nUser-Agent: Enhanced-OpenTS-Website\r\n",
    ]]);
    $body = @file_get_contents('https://api.github.com/repos/' . RELEASE_REPOSITORY . '/releases?per_page=100',
        false, $context, 0, 4 * 1024 * 1024);
    $releases = $body === false ? null : json_decode($body, true);
    $result = is_array($releases) && array_is_list($releases) ? latestPlatformReleases($releases) : null;
    if ($cache) {
        apcu_store($cacheKey, $result, $result === null ? 60 : 600);
    }
    return $result;
}
