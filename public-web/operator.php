<?php
declare(strict_types=1);
require __DIR__ . '/campaign.php';
require __DIR__ . '/access.php';

header('Cache-Control: no-store');
header("Content-Security-Policy: default-src 'none'; style-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: no-referrer');

$operator = operatorIdentity($_SERVER['PHP_AUTH_PW'] ?? '', $_SERVER['PHP_AUTH_USER'] ?? '');
if ($operator === null) {
    header('WWW-Authenticate: Basic realm="OpenTS Operator", charset="UTF-8"');
    http_response_code(401);
    exit('Operator authentication required.');
}
session_start([
    'use_strict_mode' => 1,
    'cookie_httponly' => true,
    'cookie_secure' => true,
    'cookie_samesite' => 'Strict',
]);
$_SESSION['wdt_csrf'] ??= bin2hex(random_bytes(32));
$csrf = $_SESSION['wdt_csrf'];
session_write_close();

function h(string $text): string
{
    return htmlspecialchars($text, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

$notice = '';
$battles = [];
$audit = [];
try {
    $db = database();
    if ($_SERVER['REQUEST_METHOD'] === 'POST') {
        $submitted = $_POST['csrf'] ?? null;
        if (!is_string($submitted) || !hash_equals($csrf, $submitted)) {
            http_response_code(403);
            exit('Invalid form token.');
        }
        $id = $_POST['battle'] ?? '';
        $decision = $_POST['decision'] ?? '';
        $outcome = $_POST['outcome'] ?? '';
        if (!is_string($id) || !preg_match('/\A[a-f0-9]{32}\z/', $id)) {
            http_response_code(400);
            exit('Invalid battle ID.');
        }
        $body = ['map' => $_POST['map'] ?? '', 'decision' => $decision, 'reason' => $_POST['reason'] ?? ''];
        if ($decision === 'approve' && in_array($outcome, ['GDI', 'Nod'], true)) {
            $body += ['status' => 'won', 'winner' => $outcome];
        } else {
            $body['status'] = $decision === 'approve' ? ($outcome === 'draw' ? 'draw' : '') : 'aborted';
        }
        [$status, $response] = transition($db, $id, $body, 'result', $operator);
        http_response_code($status);
        $notice = $response['error'] ?? 'Decision recorded.';
    }

    $battles = query($db, <<<'SQL'
        SELECT b.id, b.map_name, b.status, b.reason, t.name,
          CASE WHEN b.status = 'review' THEN 'Awaiting review'
               WHEN b.expires_at <= ? THEN 'Expired reservation'
               WHEN b.status = 'reserved' THEN 'Stalled reservation'
               WHEN b.status = 'in_progress' THEN 'Stalled match'
               ELSE 'Aborted match' END AS queue
        FROM wdt_battle b JOIN wdt_territory t ON t.id = b.territory_id
        WHERE b.status IN ('review', 'aborted')
           OR (b.status = 'reserved' AND b.expires_at <= ?)
           OR (b.status = 'in_progress' AND b.started_at <= ?)
        ORDER BY (b.status = 'review') DESC, b.expires_at DESC LIMIT 60
        SQL, [time(), time() + 6600, time() - 3600])->fetchAll();
    foreach ($battles as &$battle) {
        $battle['players'] = query($db, 'SELECT faction, player_name, checked_in_at, joined_at
            FROM wdt_lobby_seat WHERE battle_id = ? ORDER BY faction', [$battle['id']])->fetchAll();
    }
    unset($battle);
    $audit = query($db, 'SELECT * FROM wdt_operator_audit ORDER BY id DESC LIMIT 25')->fetchAll();
} catch (Throwable $error) {
    error_log('OpenTS operator: ' . $error->getMessage());
    http_response_code(503);
    $notice = 'The operator queue is unavailable.';
}
?>
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenTS Enhanced operator queue</title><link rel="stylesheet" href="assets/site.css"></head>
<body><main class="shell section">
<p><a href="./">← Frontier</a></p>
<h1 class="utility-title">Operator queue</h1>
<p>Signed in as <?= h($operator) ?></p>
<?php if ($notice): ?><p class="empty" role="status"><?= h($notice) ?></p><?php endif; ?>
<?php if (!$battles): ?><p class="empty">No battles in the queue.</p><?php endif; ?>
<?php foreach ($battles as $battle): ?>
<article class="operator-card">
<p class="eyebrow"><?= h($battle['queue']) ?></p>
<h3><?= h($battle['name']) ?></h3>
<p><?= h($battle['map_name']) ?> · <?= h($battle['id']) ?></p>
<?php if ($battle['reason']): ?><p><?= h($battle['reason']) ?></p><?php endif; ?>
<ul>
<?php foreach ($battle['players'] as $seat): ?>
<li><?= h($seat['faction'] . ' · ' . $seat['player_name']) ?> · <?= $seat['joined_at'] ? 'Joined game' : ($seat['checked_in_at'] ? 'Checked in' : 'Invited') ?></li>
<?php endforeach; ?>
</ul>
<?php if (!in_array($battle['queue'], ['Aborted match', 'Expired reservation'], true)): ?>
<form method="post" class="utility-form">
<input type="hidden" name="csrf" value="<?= h($csrf) ?>"><input type="hidden" name="battle" value="<?= h($battle['id']) ?>">
<input type="hidden" name="map" value="<?= h($battle['map_name']) ?>">
<label>Public reason <input name="reason" maxlength="200" required></label>
<?php if ($battle['status'] === 'review'): ?>
<label>Verified outcome <select name="outcome"><option value="">Choose a result</option><option>GDI</option><option>Nod</option><option value="draw">Draw</option></select></label>
<button name="decision" value="approve">Approve</button><button name="decision" value="reject">Reject</button>
<?php endif; ?>
<button name="decision" value="cancel">Cancel battle</button>
</form>
<?php endif; ?>
</article>
<?php endforeach; ?>
<section class="section"><h2>Operator audit</h2>
<?php if (!$audit): ?><p>No decisions recorded.</p><?php endif; ?>
<ul><?php foreach ($audit as $row): ?>
<li><?= h(gmdate('Y-m-d H:i', (int) $row['acted_at']) . ' UTC · ' . $row['operator_name'] . ' · ' . $row['action'] . ' · ' . $row['battle_id'] . ' · ' . $row['reason']) ?></li>
<?php endforeach; ?></ul></section>
</main></body></html>
