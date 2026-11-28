<?php
declare(strict_types=1);

require_once __DIR__ . '/settings.php';

function database(): PDO
{
    $dsn = webSetting('OPENTS_WEB_DSN');
    if (!$dsn || !str_starts_with($dsn, 'mysql:')) {
        throw new RuntimeException('OPENTS_WEB_DSN must specify a MariaDB database.');
    }
    return new PDO($dsn, webSetting('OPENTS_WEB_DB_USER') ?: '', webSetting('OPENTS_WEB_DB_PASSWORD') ?: '', [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES => false,
    ]);
}

function query(PDO $db, string $sql, array $params = []): PDOStatement
{
    $statement = $db->prepare($sql);
    $statement->execute($params);
    return $statement;
}

function frontierChallengers(string $id, array $owners): array
{
    $neighbors = [
        'river-crossing' => ['central-basin'],
        'central-basin' => ['river-crossing', 'ridge-line'],
        'ridge-line' => ['central-basin'],
    ];
    $home = ['GDI' => 'river-crossing', 'Nod' => 'ridge-line'];
    $challengers = [];
    foreach ($home as $faction => $homeTerritory) {
        if (($owners[$id] ?? null) === $faction) {
            continue;
        }
        if (!in_array($faction, $owners, true)) {
            if ($id === $homeTerritory) {
                $challengers[] = $faction;
            }
            continue;
        }
        foreach ($neighbors[$id] ?? [] as $neighbor) {
            if (($owners[$neighbor] ?? null) === $faction) {
                $challengers[] = $faction;
                break;
            }
        }
    }
    return $challengers;
}

function snapshot(PDO $db): array
{
    $territories = query($db, <<<'SQL'
        SELECT t.id, t.name, t.map_name AS map, t.owner, b.id AS battle_id, b.status AS battle_status, b.expires_at
        FROM wdt_territory t LEFT JOIN wdt_battle b
          ON b.active_territory = t.id AND b.expires_at > ?
        ORDER BY t.sort_order
        SQL, [time()])->fetchAll();
    $owners = array_column($territories, 'owner', 'id');
    foreach ($territories as &$territory) {
        $territory['challengers'] = frontierChallengers($territory['id'], $owners);
    }
    unset($territory);
    $history = $db->query(<<<'SQL'
        SELECT b.id, t.name AS territory, b.map_name AS map, b.status, b.winner, b.reason, b.finished_at
        FROM wdt_battle b JOIN wdt_territory t ON t.id = b.territory_id
        WHERE b.finished_at IS NOT NULL ORDER BY b.finished_at DESC, b.id DESC LIMIT 12
        SQL)->fetchAll();
    return ['id' => 'frontier', 'name' => 'Frontier', 'factions' => ['GDI', 'Nod'],
        'territories' => $territories, 'history' => $history];
}

function expire(PDO $db, string $id, int $now): void
{
    query($db, "UPDATE wdt_battle SET status = 'expired', finished_at = ?, reason = 'Reservation expired'
        WHERE active_territory = ? AND expires_at <= ?", [$now, $id, $now]);
}

function withTerritory(PDO $db, string $id, Closure $operation, bool $lockFront = false): array
{
    $db->beginTransaction();
    try {
        if ($lockFront) {
            // Lock ownership before reading the front so concurrent captures cannot change eligibility.
            $db->query('SELECT id FROM wdt_territory ORDER BY id FOR UPDATE')->fetchAll();
        }
        $territory = query($db, 'SELECT * FROM wdt_territory WHERE id = ? FOR UPDATE', [$id])->fetch();
        if ($territory) {
            $now = time();
            expire($db, $id, $now);
            $result = $operation($territory, $now);
        } else {
            $result = [404, ['error' => 'Unknown territory.']];
        }
        $db->commit();
        return $result;
    } catch (Throwable $error) {
        $db->rollBack();
        throw $error;
    }
}

function reserve(PDO $db, array $body): array
{
    $id = $body['territory'] ?? null;
    if (!is_string($id) || !preg_match('/\A[a-z-]{1,32}\z/', $id)) {
        return [400, ['error' => 'Choose a Frontier territory.']];
    }
    $players = $body['players'] ?? null;
    if (!is_array($players) || count($players) !== 2) {
        return [400, ['error' => 'Provide one invited player for GDI and one for Nod.']];
    }
    $seats = [];
    foreach (['GDI', 'Nod'] as $faction) {
        $seat = $players[$faction] ?? null;
        $name = is_array($seat) ? ($seat['name'] ?? null) : null;
        $connection = is_array($seat) ? ($seat['connection'] ?? null) : null;
        $host = is_array($connection) ? ($connection['host'] ?? null) : null;
        $port = is_array($connection) ? ($connection['port'] ?? null) : null;
        if (!is_string($name) || trim($name) === '' || strlen($name) > 80
            || !is_string($host) || !preg_match('~\A[a-zA-Z0-9.:-]{1,253}\z~', $host)
            || !is_int($port) || $port < 1 || $port > 65535) {
            return [400, ['error' => 'Each faction needs a player name and a connection host and port.']];
        }
        $seats[$faction] = ['name' => trim($name), 'connection' => ['host' => $host, 'port' => $port]];
    }
    if (strcasecmp($seats['GDI']['name'], $seats['Nod']['name']) === 0) {
        return [400, ['error' => 'Invite two different players.']];
    }

    return withTerritory($db, $id, function (array $territory, int $now) use ($db, $id, $seats): array {
        $existing = query($db, 'SELECT id FROM wdt_battle WHERE active_territory = ?', [$id])->fetchColumn();
        if ($existing !== false) {
            return [409, ['error' => 'Territory already has an active battle.', 'battle_id' => $existing]];
        }
        $owners = $db->query('SELECT id, owner FROM wdt_territory')->fetchAll(PDO::FETCH_KEY_PAIR);
        if (!frontierChallengers($id, $owners)) {
            return [409, ['error' => 'This territory is behind the front line. Choose an open challenge.']];
        }
        $battle = bin2hex(random_bytes(16));
        $map = $territory['map_name'];
        $expires = $now + 7200;
        query($db, "INSERT INTO wdt_battle (id, territory_id, map_name, status, expires_at)
            VALUES (?, ?, ?, 'reserved', ?)", [$battle, $id, $map, $expires]);
        $invites = [];
        foreach ($seats as $faction => $seat) {
            $token = bin2hex(random_bytes(32));
            query($db, 'INSERT INTO wdt_lobby_seat (battle_id, faction, player_name, invite_hash, connection_json)
                VALUES (?, ?, ?, ?, ?)', [$battle, $faction, $seat['name'], hash('sha256', $token, true),
                json_encode($seat['connection'], JSON_THROW_ON_ERROR)]);
            $invites[$faction] = $token;
        }
        return [201, ['invites' => $invites, 'id' => $battle, 'territory' => $id, 'map' => $map, 'factions' => ['GDI', 'Nod'], 'status' => 'reserved', 'expires_at' => $expires]];
    }, lockFront: true);
}

function transition(PDO $db, string $id, array $body, string $operation, ?string $operator = null): array
{
    $status = match ($operation) {
        'start' => 'in_progress',
        'review' => 'review',
        default => $body['status'] ?? null,
    };
    $decision = $body['decision'] ?? null;
    if ($operator !== null && (!in_array($decision, ['approve', 'reject', 'cancel'], true)
        || ($decision === 'approve' ? !in_array($status, ['won', 'draw'], true) : $status !== 'aborted'))) {
        return [400, ['error' => 'Choose a valid operator decision.']];
    }
    $winner = $body['winner'] ?? null;
    $map = $body['map'] ?? null;
    $reason = $body['reason'] ?? '';
    if (!in_array($status, ['in_progress', 'review', 'won', 'draw', 'aborted'], true)
        || ($operation === 'result' && !in_array($status, ['won', 'draw', 'aborted'], true))
        || !is_string($map) || !is_string($reason) || strlen($reason) > 200
        || ($status === 'won' ? !in_array($winner, ['GDI', 'Nod'], true) : $winner !== null)
        || (in_array($status, ['review', 'won', 'draw', 'aborted'], true) && trim($reason) === '')) {
        return [400, ['error' => 'Provide the assigned map, a valid outcome, and a reason for reviews, wins and aborts.']];
    }

    if ($operator === null && in_array($status, ['won', 'draw'], true)) {
        return [403, ['error' => 'An operator must approve the outcome.']];
    }

    $territory = query($db, 'SELECT territory_id FROM wdt_battle WHERE id = ?', [$id])->fetchColumn();
    if ($territory === false) {
        return [404, ['error' => 'Unknown battle.']];
    }

    return withTerritory($db, $territory, function (array $territory, int $now) use ($db, $id, $status, $winner, $map, $reason, $operator, $decision): array {
        $battle = query($db, 'SELECT * FROM wdt_battle WHERE id = ? FOR UPDATE', [$id])->fetch();
        if ($battle['map_name'] !== $map) {
            return [409, ['error' => 'Map differs from the reserved battle map.']];
        }
        $response = [200, ['id' => $id, 'status' => $status, 'winner' => $winner]];
        if ($battle['status'] === $status && $battle['winner'] === $winner) {
            return $response;
        }
        $allowed = match ($status) {
            'in_progress' => ['reserved'],
            'review' => ['in_progress'],
            'won', 'draw' => ['review'],
            'aborted' => ['reserved', 'in_progress', 'review'],
        };
        if ($decision === 'reject') {
            $allowed = ['review'];
        } elseif ($operator === null && $status === 'aborted') {
            $allowed = ['reserved', 'in_progress'];
        }
        if (!in_array($battle['status'], $allowed, true)) {
            return [409, ['error' => 'Battle cannot make that transition.', 'status' => $battle['status']]];
        }

        if ($status === 'in_progress') {
            query($db, 'UPDATE wdt_battle SET status = ?, started_at = ? WHERE id = ?', [$status, $now, $id]);
        } elseif ($status === 'review') {
            query($db, 'UPDATE wdt_battle SET status = ?, reason = ? WHERE id = ?', [$status, $reason, $id]);
        } else {
            query($db, 'UPDATE wdt_battle SET status = ?, winner = ?, reason = ?, finished_at = ? WHERE id = ?',
                [$status, $winner, $reason ?: null, $now, $id]);
        }
        if ($status === 'won') {
            query($db, 'UPDATE wdt_territory SET owner = ? WHERE id = ?', [$winner, $territory['id']]);
        }
        if ($operator !== null) {
            query($db, 'INSERT INTO wdt_operator_audit (battle_id, operator_name, action, reason, acted_at)
                VALUES (?, ?, ?, ?, ?)', [$id, $operator, $decision, $reason, $now]);
        }
        return $response;
    });
}

function checkIn(PDO $db, string $token): array
{
    if (!preg_match('/\A[a-f0-9]{64}\z/', $token)) {
        return [404, ['error' => 'Invite unavailable.']];
    }
    $hash = hash('sha256', $token, true);
    $target = query($db, 'SELECT b.territory_id FROM wdt_lobby_seat s
        JOIN wdt_battle b ON b.id = s.battle_id WHERE s.invite_hash = ?', [$hash])->fetchColumn();
    if ($target === false) {
        return [404, ['error' => 'Invite unavailable.']];
    }
    return withTerritory($db, $target, function (array $territory, int $now) use ($db, $hash): array {
        $seat = query($db, "SELECT s.*, b.map_name, b.status FROM wdt_lobby_seat s
            JOIN wdt_battle b ON b.id = s.battle_id WHERE s.invite_hash = ? FOR UPDATE", [$hash])->fetch();
        if (!$seat || !in_array($seat['status'], ['reserved', 'in_progress'], true)) {
            return [404, ['error' => 'Invite unavailable.']];
        }
        query($db, 'UPDATE wdt_lobby_seat SET checked_in_at = COALESCE(checked_in_at, ?) WHERE invite_hash = ?', [$now, $hash]);
        return [200, ['battle' => $seat['battle_id'], 'territory' => $territory['name'], 'map' => $seat['map_name'],
            'player' => $seat['player_name'], 'faction' => $seat['faction'], 'status' => $seat['status'],
            'connection' => json_decode($seat['connection_json'], true, 8, JSON_THROW_ON_ERROR)]];
    });
}

function confirmJoined(PDO $db, string $battleId, array $body): array
{
    $faction = $body['faction'] ?? null;
    if (!in_array($faction, ['GDI', 'Nod'], true) || !is_string($body['player'] ?? null)) {
        return [400, ['error' => 'Provide the faction and exact roster name.']];
    }
    $target = query($db, 'SELECT territory_id FROM wdt_battle WHERE id = ?', [$battleId])->fetchColumn();
    if ($target === false) {
        return [404, ['error' => 'Unknown battle.']];
    }
    return withTerritory($db, $target, function (array $territory, int $now) use ($db, $battleId, $faction, $body): array {
        $seat = query($db, 'SELECT s.player_name, s.checked_in_at, b.status FROM wdt_lobby_seat s
            JOIN wdt_battle b ON b.id = s.battle_id WHERE s.battle_id = ? AND s.faction = ? FOR UPDATE',
            [$battleId, $faction])->fetch();
        if (!$seat || $seat['player_name'] !== $body['player'] || !$seat['checked_in_at'] || $seat['status'] !== 'in_progress') {
            return [409, ['error' => 'The named player must check in to an active match first.']];
        }
        query($db, 'UPDATE wdt_lobby_seat SET joined_at = COALESCE(joined_at, ?) WHERE battle_id = ? AND faction = ?',
            [$now, $battleId, $faction]);
        return [200, ['battle' => $battleId, 'faction' => $faction, 'joined' => true]];
    });
}
