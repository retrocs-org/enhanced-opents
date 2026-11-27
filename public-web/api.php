<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

require __DIR__ . '/campaign.php';
require __DIR__ . '/access.php';

function respond(int $status, array $data): never
{
    http_response_code($status);
    echo json_encode($data, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES), "\n";
    exit;
}

function input(): array
{
    $stream = fopen('php://input', 'rb');
    $bytes = stream_get_contents($stream, 8193);
    fclose($stream);
    if (strlen($bytes) > 8192) {
        respond(413, ['error' => 'Request is too large.']);
    }
    try {
        $decoded = json_decode($bytes, true, 16, JSON_THROW_ON_ERROR);
    } catch (JsonException) {
        respond(400, ['error' => 'Expected a JSON object.']);
    }
    if (!is_array($decoded) || array_is_list($decoded)) {
        respond(400, ['error' => 'Expected a JSON object.']);
    }
    return $decoded;
}

function requireServer(): void
{
    $expected = webSetting('OPENTS_WDT_SERVER_TOKEN');
    $provided = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    if (strlen($expected) < 32) {
        respond(503, ['error' => 'Server authentication is unconfigured.']);
    }
    if (!hash_equals('Bearer ' . $expected, $provided)) {
        respond(401, ['error' => 'Server authentication required.']);
    }
}

try {
    $method = $_SERVER['REQUEST_METHOD'] ?? '';
    $route = $_GET['route'] ?? 'theater';
    if ($method === 'GET' && $route === 'theater') {
        $campaign = snapshot(database());
        if (($_GET['format'] ?? null) === 'briefing') {
            header('Content-Type: text/plain; charset=utf-8');
            echo "FRONTIER — WORLD DOMINATION\n\n";
            foreach ($campaign['territories'] as $territory) {
                echo $territory['name'], ' · ', $territory['owner'];
                if ($territory['battle_status']) echo ' · ', str_replace('_', ' ', $territory['battle_status']);
                echo "\n";
            }
            echo "\nRECENT BATTLES\n";
            if (!$campaign['history']) echo "No completed battles yet.\n";
            foreach (array_slice($campaign['history'], 0, 3) as $battle) {
                echo $battle['territory'], ' · ', $battle['winner'] ? $battle['winner'] . ' victory' : $battle['status'], "\n";
            }
            exit;
        }
        respond(200, $campaign);
    }
    if ($method === 'POST' && $route === 'battles') {
        requireServer();
        [$status, $body] = reserve(database(), input());
        respond($status, $body);
    }
    if ($method === 'POST' && is_string($route) && preg_match('~\Abattles/([a-f0-9]{32})/(start|review|result|joined|decision)\z~', $route, $matches)) {
        $operation = $matches[2];
        if ($operation === 'decision') {
            $authorization = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
            $operator = str_starts_with($authorization, 'Bearer ') ? operatorIdentity(substr($authorization, 7)) : null;
            if ($operator === null) respond(401, ['error' => 'Operator authentication required.']);
            [$status, $body] = transition(database(), $matches[1], input(), 'result', $operator);
        } else {
            requireServer();
            [$status, $body] = $operation === 'joined'
                ? confirmJoined(database(), $matches[1], input())
                : transition(database(), $matches[1], input(), $operation);
        }
        respond($status, $body);
    }
    respond(404, ['error' => 'Endpoint not found.']);
} catch (Throwable $error) {
    error_log('OpenTS World Domination: ' . $error->getMessage());
    respond(503, ['error' => 'Theater service unavailable.']);
}
