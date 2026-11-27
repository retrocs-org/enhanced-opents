<?php
declare(strict_types=1);
require __DIR__ . '/campaign.php';

header('Cache-Control: no-store');
header('Referrer-Policy: no-referrer');
header("Content-Security-Policy: default-src 'none'; style-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");

$seat = null;
$error = null;
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    try {
        $code = $_POST['invite'] ?? '';
        [$status, $result] = checkIn(database(), is_string($code) ? trim($code) : '');
        http_response_code($status);
        if ($status === 200) {
            $seat = $result;
        } else {
            $error = $result['error'];
        }
    } catch (Throwable $failure) {
        error_log('OpenTS lobby: ' . $failure->getMessage());
        http_response_code(503);
        $error = 'The lobby is unavailable.';
    }
}
function h(string $text): string
{
    return htmlspecialchars($text, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
?>
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenTS Enhanced private lobby</title><link rel="stylesheet" href="assets/site.css"></head>
<body><main class="shell section">
<p><a href="./">← Frontier</a></p><h1 class="utility-title">Private match lobby</h1>
<?php if ($error): ?><p class="empty" role="alert"><?= h($error) ?></p><?php endif; ?>
<?php if ($seat): ?>
<article class="operator-card"><p class="eyebrow"><?= h($seat['faction']) ?></p>
<h2><?= h($seat['territory']) ?></h2><p><?= h($seat['player']) ?> · <?= h($seat['map']) ?></p>
<dl><dt>Host</dt><dd><?= h($seat['connection']['host']) ?></dd><dt>Port</dt><dd><?= (int) $seat['connection']['port'] ?></dd></dl>
<p>Checked in. Use these details with your match host.</p></article>
<?php else: ?>
<form method="post" class="utility-form">
<label>Invite code <input name="invite" type="password" required maxlength="64" autocomplete="off"></label>
<button>Check in</button>
</form>
<?php endif; ?>
</main></body></html>
