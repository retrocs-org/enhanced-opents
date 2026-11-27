<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

const TERRITORIES = [
    'river-crossing' => ['River Crossing', 'WDTRIVER.MPR', 'GDI'],
    'central-basin' => ['Central Basin', 'WDTBASIN.MPR', 'Neutral'],
    'ridge-line' => ['Ridge Line', 'WDTRIDGE.MPR', 'Nod'],
];

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

function query(PDO $db, string $sql, array $values = []): PDOStatement
{
    $statement = $db->prepare($sql);
    $statement->execute($values);
    return $statement;
}

function requireServer(): void
{
    $expected = getenv('OPENTS_WDT_SERVER_TOKEN');
    $provided = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    if ($expected === false || strlen($expected) < 32) {
        respond(503, ['error' => 'Server authentication is unconfigured.']);
    }
    if (!hash_equals('Bearer ' . $expected, $provided)) {
        respond(401, ['error' => 'Server authentication required.']);
    }
}

function openDatabase(): PDO
{
    $path = getenv('OPENTS_WDT_DATABASE');
    if (!$path || !str_starts_with($path, DIRECTORY_SEPARATOR)) {
        throw new RuntimeException('Set OPENTS_WDT_DATABASE to an absolute file path outside the web root.');
    }
    $db = new PDO('sqlite:' . $path, null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
    $db->exec('PRAGMA foreign_keys = ON');
    $db->exec('PRAGMA busy_timeout = 5000');
    $db->exec(<<<'SQL'
        CREATE TABLE IF NOT EXISTS territory (
            id TEXT PRIMARY KEY,
            owner TEXT NOT NULL CHECK (owner IN ('GDI', 'Nod', 'Neutral'))
        );
        CREATE TABLE IF NOT EXISTS battle (
            id TEXT PRIMARY KEY,
            territory_id TEXT NOT NULL REFERENCES territory(id),
            status TEXT NOT NULL CHECK (status IN ('active', 'won', 'draw', 'aborted', 'expired')),
            winner TEXT CHECK (winner IN ('GDI', 'Nod')),
            expires_at INTEGER NOT NULL,
            finished_at INTEGER
        );
        CREATE UNIQUE INDEX IF NOT EXISTS one_active_battle
            ON battle(territory_id) WHERE status = 'active';
        SQL);
    foreach (TERRITORIES as $id => [, , $owner]) {
        query($db, 'INSERT OR IGNORE INTO territory (id, owner) VALUES (?, ?)', [$id, $owner]);
    }
    return $db;
}

function theater(PDO $db, int $now): array
{
    $territories = [];
    $rows = query($db, <<<'SQL'
        SELECT t.id, t.owner, b.id AS battle_id, b.expires_at
        FROM territory AS t
        LEFT JOIN battle AS b ON b.territory_id = t.id AND b.status = 'active' AND b.expires_at > ?
        SQL, [$now])->fetchAll(PDO::FETCH_UNIQUE);
    foreach (TERRITORIES as $id => [$name, $map]) {
        $row = $rows[$id];
        $territories[] = ['id' => $id, 'name' => $name, 'map' => $map] + $row;
    }
    return ['id' => 'frontier', 'name' => 'Frontier', 'factions' => ['GDI', 'Nod'], 'territories' => $territories];
}

function mutation(PDO $db, Closure $handler): array
{
    $db->exec('BEGIN IMMEDIATE');
    try {
        $now = time();
        query($db, "UPDATE battle SET status = 'expired', finished_at = ? WHERE status = 'active' AND expires_at <= ?", [$now, $now]);
        $result = $handler($now);
        $db->exec('COMMIT');
        return $result;
    } catch (Throwable $error) {
        $db->exec('ROLLBACK');
        throw $error;
    }
}

function reserve(PDO $db, array $body): array
{
    $territory = $body['territory'] ?? null;
    if (!is_string($territory) || !isset(TERRITORIES[$territory])) {
        return [400, ['error' => 'Choose a Frontier territory.']];
    }

    return mutation($db, function (int $now) use ($db, $territory): array {
        $existing = query($db, "SELECT id FROM battle WHERE territory_id = ? AND status = 'active'", [$territory])->fetchColumn();
        if ($existing !== false) {
            return [409, ['error' => 'Territory already has an active battle.', 'battle_id' => $existing]];
        }
        $id = bin2hex(random_bytes(16));
        $expires = $now + 7200;
        query($db, "INSERT INTO battle (id, territory_id, status, expires_at) VALUES (?, ?, 'active', ?)", [$id, $territory, $expires]);
        return [201, ['id' => $id, 'territory' => $territory, 'map' => TERRITORIES[$territory][1],
            'factions' => ['GDI', 'Nod'], 'expires_at' => $expires]];
    });
}

function report(PDO $db, string $id, array $body): array
{
    $status = $body['status'] ?? null;
    $winner = $body['winner'] ?? null;
    $map = $body['map'] ?? null;
    if (!in_array($status, ['won', 'draw', 'aborted'], true)
        || ($status === 'won' ? !in_array($winner, ['GDI', 'Nod'], true) : $winner !== null)
        || !is_string($map)) {
        return [400, ['error' => 'Provide the outcome, assigned map, and winning faction when applicable.']];
    }

    return mutation($db, function (int $now) use ($db, $id, $status, $winner, $map): array {
        $battle = query($db, 'SELECT * FROM battle WHERE id = ?', [$id])->fetch();
        if (!$battle) {
            return [404, ['error' => 'Battle does not exist.']];
        }
        if ($map !== TERRITORIES[$battle['territory_id']][1]) {
            return [409, ['error' => 'Map differs from the reserved battle map.']];
        }
        if ($battle['status'] === $status && $battle['winner'] === $winner) {
            return [200, ['id' => $id, 'status' => $status, 'winner' => $winner]];
        }
        if ($battle['status'] !== 'active') {
            return [409, ['error' => 'Battle is already closed.', 'status' => $battle['status']]];
        }
        query($db, 'UPDATE battle SET status = ?, winner = ?, finished_at = ? WHERE id = ?', [$status, $winner, $now, $id]);
        if ($status === 'won') {
            query($db, 'UPDATE territory SET owner = ? WHERE id = ?', [$winner, $battle['territory_id']]);
        }
        return [200, ['id' => $id, 'status' => $status, 'winner' => $winner]];
    });
}

try {
    $method = $_SERVER['REQUEST_METHOD'] ?? '';
    $route = $_GET['route'] ?? 'theater';
    if ($method === 'GET' && $route === 'theater') {
        respond(200, theater(openDatabase(), time()));
    }
    if ($method === 'POST' && $route === 'battles') {
        requireServer();
        [$status, $body] = reserve(openDatabase(), input());
        respond($status, $body);
    }
    if ($method === 'POST' && is_string($route) && preg_match('~\Abattles/([a-f0-9]{32})/result\z~', $route, $matches)) {
        requireServer();
        [$status, $body] = report(openDatabase(), $matches[1], input());
        respond($status, $body);
    }
    respond(404, ['error' => 'Endpoint not found.']);
} catch (Throwable $error) {
    error_log('OpenTS World Domination: ' . $error->getMessage());
    respond(503, ['error' => 'Theater service unavailable.']);
}
